#include "shader_artifact_cache.hpp"

#include "rendering/shader_compiler.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

namespace {

constexpr std::uint64_t ShaderCacheMagic = 0x4548434143454946ULL;
constexpr std::uint32_t ShaderCacheVersion = 1;
constexpr std::uint64_t MaxCacheCollectionSize = 1'000'000;
constexpr std::uint64_t MaxCacheStringSize = 64ULL * 1024 * 1024;
constexpr std::uint64_t MaxCacheBlobSize = 512ULL * 1024 * 1024;

class StableHasher {
  private:
    std::uint64_t m_value {14695981039346656037ULL};

  public:
    void add_bytes(std::span<const std::byte> bytes) {
        for (auto byte : bytes) {
            m_value ^= std::to_integer<std::uint8_t>(byte);
            m_value *= 1099511628211ULL;
        }
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void add(const T& value) {
        add_bytes(std::as_bytes(std::span(&value, 1)));
    }

    void add(std::string_view value) {
        add(static_cast<std::uint64_t>(value.size()));
        add_bytes(std::as_bytes(std::span(value.data(), value.size())));
    }

    [[nodiscard]] std::uint64_t value() const { return m_value; }
};

class CacheWriter {
  private:
    std::vector<std::byte> m_bytes;

  public:
    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void write(const T& value) {
        auto offset = m_bytes.size();
        m_bytes.resize(offset + sizeof(value));
        std::memcpy(m_bytes.data() + offset, &value, sizeof(value));
    }

    void write(std::string_view value) {
        write(static_cast<std::uint64_t>(value.size()));
        auto offset = m_bytes.size();
        m_bytes.resize(offset + value.size());
        std::memcpy(m_bytes.data() + offset, value.data(), value.size());
    }

    void write(std::span<const std::byte> value) {
        write(static_cast<std::uint64_t>(value.size()));
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
    }

    [[nodiscard]] const std::vector<std::byte>& bytes() const {
        return m_bytes;
    }
};

class CacheReader {
  private:
    std::span<const std::byte> m_bytes;
    std::size_t m_offset {0};

  public:
    explicit CacheReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    bool read(T& value) {
        if (sizeof(value) > m_bytes.size() - m_offset) {
            return false;
        }
        std::memcpy(&value, m_bytes.data() + m_offset, sizeof(value));
        m_offset += sizeof(value);
        return true;
    }

    bool read(std::string& value) {
        std::uint64_t size = 0;
        if (!read(size) || size > MaxCacheStringSize ||
            size > m_bytes.size() - m_offset) {
            return false;
        }
        value.assign(
            reinterpret_cast<const char*>(m_bytes.data() + m_offset),
            static_cast<std::size_t>(size)
        );
        m_offset += static_cast<std::size_t>(size);
        return true;
    }

    bool read(std::vector<std::byte>& value) {
        std::uint64_t size = 0;
        if (!read(size) || size > MaxCacheBlobSize ||
            size > m_bytes.size() - m_offset) {
            return false;
        }
        value.assign(
            m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
            m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + size)
        );
        m_offset += static_cast<std::size_t>(size);
        return true;
    }

    bool read_size(std::uint64_t& size) {
        return read(size) && size <= MaxCacheCollectionSize;
    }

    [[nodiscard]] bool finished() const { return m_offset == m_bytes.size(); }
};

std::optional<std::uint64_t>
shader_file_hash(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    StableHasher hasher;
    std::array<char, std::size_t {16} * 1024> buffer {};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto size = input.gcount();
        hasher.add_bytes(
            std::as_bytes(
                std::span(buffer.data(), static_cast<std::size_t>(size))
            )
        );
    }
    if (!input.eof()) {
        return std::nullopt;
    }
    return hasher.value();
}

std::uint64_t shader_source_hash(std::string_view source) {
    StableHasher hasher;
    hasher.add_bytes(std::as_bytes(std::span(source.data(), source.size())));
    return hasher.value();
}

std::filesystem::path normalized_absolute_path(std::filesystem::path path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (!error) {
        path = std::move(absolute);
    }
    return path.lexically_normal();
}

void hash_shader_def(StableHasher& hasher, const ShaderDefVal& def) {
    hasher.add(def.name);
    hasher.add(static_cast<std::uint8_t>(def.value.index()));
    std::visit(
        [&](const auto& value) {
            hasher.add(value);
        },
        def.value
    );
}

std::optional<std::uint64_t> shader_cache_key(
    const ShaderCompileRequest& request,
    std::string_view compiler_identity
) {
    StableHasher hasher;
    hasher.add(ShaderCacheVersion);
    hasher.add(compiler_identity);
    hasher.add(request.logical_path.generic_string());
    hasher.add(request.source_path.generic_string());
    hasher.add(request.source_root.generic_string());
    hasher.add(static_cast<std::uint8_t>(request.stage));
    hasher.add(request.entry);
    hasher.add(static_cast<std::uint64_t>(request.search_roots.size()));
    for (const auto& root : request.search_roots) {
        hasher.add(root.generic_string());
    }
    hasher.add(static_cast<std::uint64_t>(request.defs.size()));
    for (const auto& def : request.defs) {
        hash_shader_def(hasher, def);
    }

    if (!request.source.empty()) {
        hasher.add(std::uint8_t {1});
        hasher.add(request.source);
    } else {
        hasher.add(std::uint8_t {0});
        auto source_hash = shader_file_hash(request.source_path);
        if (!source_hash) {
            return std::nullopt;
        }
        hasher.add(*source_hash);
    }
    return hasher.value();
}

void write_shader_def(CacheWriter& writer, const ShaderDefVal& def) {
    writer.write(def.name);
    writer.write(static_cast<std::uint8_t>(def.value.index()));
    std::visit(
        [&](const auto& value) {
            writer.write(value);
        },
        def.value
    );
}

bool read_shader_def(CacheReader& reader, ShaderDefVal& def) {
    std::uint8_t kind = 0;
    if (!reader.read(def.name) || !reader.read(kind)) {
        return false;
    }
    switch (kind) {
        case 0: {
            bool value = false;
            if (!reader.read(value)) {
                return false;
            }
            def.value = value;
            return true;
        }
        case 1: {
            std::int32_t value = 0;
            if (!reader.read(value)) {
                return false;
            }
            def.value = value;
            return true;
        }
        case 2: {
            std::uint32_t value = 0;
            if (!reader.read(value)) {
                return false;
            }
            def.value = value;
            return true;
        }
        default:
            return false;
    }
}

void write_shader_description(
    CacheWriter& writer,
    const ShaderDescription& description
) {
    writer.write(static_cast<std::uint8_t>(description.stage));
    writer.write(description.source);
    writer.write(std::span(description.spirv));
    writer.write(description.path);
    writer.write(static_cast<std::uint64_t>(description.resources.size()));
    for (const auto& resource : description.resources) {
        writer.write(resource.name);
        writer.write(resource.backend_name);
        writer.write(static_cast<std::uint64_t>(resource.backend_names.size()));
        for (const auto& name : resource.backend_names) {
            writer.write(name);
        }
        writer.write(
            static_cast<std::underlying_type_t<ResourceKind>>(resource.kind)
        );
        writer.write(resource.set);
        writer.write(resource.binding);
        writer.write(resource.array_size);
    }
    writer.write(static_cast<std::uint64_t>(description.defs.size()));
    for (const auto& def : description.defs) {
        write_shader_def(writer, def);
    }
}

bool read_shader_description(
    CacheReader& reader,
    ShaderDescription& description
) {
    std::uint8_t stage = 0;
    if (!reader.read(stage) || !reader.read(description.source) ||
        !reader.read(description.spirv) || !reader.read(description.path)) {
        return false;
    }
    description.stage = static_cast<ShaderStages>(stage);

    std::uint64_t resource_count = 0;
    if (!reader.read_size(resource_count)) {
        return false;
    }
    description.resources.resize(static_cast<std::size_t>(resource_count));
    for (auto& resource : description.resources) {
        std::uint64_t backend_name_count = 0;
        std::underlying_type_t<ResourceKind> kind {};
        if (!reader.read(resource.name) ||
            !reader.read(resource.backend_name) ||
            !reader.read_size(backend_name_count)) {
            return false;
        }
        resource.backend_names.resize(
            static_cast<std::size_t>(backend_name_count)
        );
        for (auto& name : resource.backend_names) {
            if (!reader.read(name)) {
                return false;
            }
        }
        if (!reader.read(kind) || !reader.read(resource.set) ||
            !reader.read(resource.binding) ||
            !reader.read(resource.array_size)) {
            return false;
        }
        resource.kind = static_cast<ResourceKind>(kind);
    }

    std::uint64_t def_count = 0;
    if (!reader.read_size(def_count)) {
        return false;
    }
    description.defs.resize(static_cast<std::size_t>(def_count));
    for (auto& def : description.defs) {
        if (!read_shader_def(reader, def)) {
            return false;
        }
    }
    return true;
}

struct CachedDependency {
    std::filesystem::path path;
    std::uint64_t content_hash;
};

std::optional<std::vector<CachedDependency>> cache_dependencies(
    const ShaderCompileRequest& request,
    const ShaderCompileOutput& output
) {
    auto dependencies = output.dependencies;
    for (auto& dependency : dependencies) {
        dependency = normalized_absolute_path(std::move(dependency));
    }
    std::ranges::sort(dependencies);
    dependencies.erase(
        std::ranges::unique(dependencies).begin(),
        dependencies.end()
    );

    const auto source_path = normalized_absolute_path(request.source_path);
    std::vector<CachedDependency> cached_dependencies;
    cached_dependencies.reserve(dependencies.size());
    for (const auto& dependency : dependencies) {
        std::optional<std::string_view> consumed_source;
        if (!request.source.empty() && dependency == source_path) {
            consumed_source = request.source;
        }
        for (const auto& snapshot : output.dependency_snapshots) {
            if (normalized_absolute_path(snapshot.path) != dependency) {
                continue;
            }
            if (consumed_source && *consumed_source != snapshot.source) {
                return std::nullopt;
            }
            consumed_source = snapshot.source;
        }
        if (!consumed_source) {
            return std::nullopt;
        }

        auto content_hash = shader_source_hash(*consumed_source);
        auto current_hash = shader_file_hash(dependency);
        if (!current_hash || *current_hash != content_hash) {
            return std::nullopt;
        }
        cached_dependencies.push_back(
            CachedDependency {
                .path = dependency,
                .content_hash = content_hash,
            }
        );
    }
    return cached_dependencies;
}

std::optional<ShaderVariantCompileOutput> load_shader_cache(
    const std::filesystem::path& path,
    std::uint64_t expected_key
) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::vector<char> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char> {}
    );
    CacheReader reader(std::as_bytes(std::span(bytes)));
    std::uint64_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t key = 0;
    ShaderDescription description;
    if (!reader.read(magic) || !reader.read(version) || !reader.read(key) ||
        magic != ShaderCacheMagic || version != ShaderCacheVersion ||
        key != expected_key || !read_shader_description(reader, description)) {
        return std::nullopt;
    }

    std::uint64_t dependency_count = 0;
    if (!reader.read_size(dependency_count)) {
        return std::nullopt;
    }
    std::vector<std::filesystem::path> dependencies;
    dependencies.reserve(static_cast<std::size_t>(dependency_count));
    for (std::uint64_t i = 0; i < dependency_count; ++i) {
        std::string dependency;
        std::uint64_t expected_hash = 0;
        if (!reader.read(dependency) || !reader.read(expected_hash)) {
            return std::nullopt;
        }
        auto path = std::filesystem::path(dependency);
        auto current_hash = shader_file_hash(path);
        if (!current_hash || *current_hash != expected_hash) {
            return std::nullopt;
        }
        dependencies.push_back(std::move(path));
    }
    if (!reader.finished()) {
        return std::nullopt;
    }
    return ShaderVariantCompileOutput {
        .description = std::move(description),
        .dependencies = std::move(dependencies),
    };
}

void save_shader_cache(
    const std::filesystem::path& path,
    std::uint64_t key,
    const ShaderCompileRequest& request,
    const ShaderCompileOutput& output
) {
    auto dependencies = cache_dependencies(request, output);
    if (!dependencies) {
        return;
    }

    CacheWriter writer;
    writer.write(ShaderCacheMagic);
    writer.write(ShaderCacheVersion);
    writer.write(key);
    write_shader_description(writer, output.description);
    writer.write(static_cast<std::uint64_t>(dependencies->size()));
    for (const auto& dependency : *dependencies) {
        writer.write(dependency.path.generic_string());
        writer.write(dependency.content_hash);
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return;
    }
    auto temporary_path = path;
    temporary_path += ".tmp";
    {
        std::ofstream stream(
            temporary_path,
            std::ios::binary | std::ios::trunc
        );
        if (!stream) {
            return;
        }
        const auto& bytes = writer.bytes();
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (!stream) {
            return;
        }
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary_path, path, error);
    if (error) {
        std::filesystem::remove(temporary_path, error);
    }
}

} // namespace

ShaderArtifactCache::ShaderArtifactCache(
    std::filesystem::path root,
    std::string compiler_identity
) :
    m_root(std::move(root)), m_compiler_identity(std::move(compiler_identity)) {
}

std::optional<ShaderArtifactCache::Key>
ShaderArtifactCache::key(const ShaderCompileRequest& request) const {
    auto value = shader_cache_key(request, m_compiler_identity);
    if (!value) {
        return std::nullopt;
    }
    return Key {
        .value = *value,
        .path = m_root / std::format("{:016x}.bin", *value),
    };
}

std::optional<ShaderVariantCompileOutput>
ShaderArtifactCache::load(const Key& key) const {
    return load_shader_cache(key.path, key.value);
}

void ShaderArtifactCache::store(
    const Key& key,
    const ShaderCompileRequest& request,
    const ShaderCompileOutput& output
) const {
    save_shader_cache(key.path, key.value, request, output);
}

} // namespace fei
