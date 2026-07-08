#include "rendering/shader.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {

namespace {

std::string_view generated_shader_sources_text() {
#ifdef FEI_SHADER_SOURCES
    return FEI_SHADER_SOURCES;
#else
    return {};
#endif
}

AssetLoadError
shader_load_error(const AssetPath& asset_path, std::string message) {
    return AssetLoadError(asset_path, std::move(message));
}

AssetLoadError
shader_load_error(const LoadContext& context, std::string message) {
    return shader_load_error(context.asset_path(), std::move(message));
}

std::vector<std::filesystem::path> root_relative_paths(
    const std::filesystem::path& path,
    std::string_view prefix
) {
    if (path.empty() || path.is_absolute()) {
        return {};
    }

    if (prefix.empty()) {
        return {path};
    }

    auto it = path.begin();
    if (it == path.end() || it->generic_string() != prefix) {
        return {};
    }

    std::vector<std::filesystem::path> paths {path};

    std::filesystem::path stripped;
    for (++it; it != path.end(); ++it) {
        stripped /= *it;
    }
    if (!stripped.empty() && stripped != path) {
        paths.push_back(std::move(stripped));
    }
    return paths;
}

std::vector<std::filesystem::path>
shader_source_candidates(const std::filesystem::path& path) {
    if (path.extension() == ".slang") {
        return {path};
    }

    if (!shader_stage_from_path(path)) {
        return {};
    }

    auto stage_specific_source = path;
    stage_specific_source += ".slang";
    auto shared_source = path;
    shared_source.replace_extension(".slang");
    std::vector<std::filesystem::path> candidates {
        stage_specific_source,
        shared_source,
    };
    candidates.erase(
        std::unique(candidates.begin(), candidates.end()),
        candidates.end()
    );
    return candidates;
}

void add_shader_source_entry(
    ShaderSourceRegistry& registry,
    std::string_view entry
) {
    const auto separator = entry.find('=');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= entry.size()) {
        return;
    }

    registry.add_root(
        std::string(entry.substr(0, separator)),
        std::filesystem::path(std::string(entry.substr(separator + 1)))
    );
}

} // namespace

void ShaderSourceRegistry::add_root(
    std::string prefix,
    std::filesystem::path root
) {
    auto normalized_prefix =
        std::filesystem::path(prefix).lexically_normal().generic_string();
    if (normalized_prefix == ".") {
        normalized_prefix.clear();
    }
    m_roots.push_back(
        ShaderSourceRoot {
            .prefix = std::move(normalized_prefix),
            .root = std::move(root),
        }
    );
}

Optional<ResolvedShaderSource>
ShaderSourceRegistry::resolve(const std::filesystem::path& path) const {
    for (const auto& source : m_roots) {
        for (auto relative : root_relative_paths(path, source.prefix)) {
            for (auto candidate : shader_source_candidates(relative)) {
                auto source_path = source.root / candidate;
                if (!std::filesystem::exists(source_path)) {
                    continue;
                }

                return ResolvedShaderSource {
                    .prefix = source.prefix,
                    .root = source.root,
                    .relative_path = std::move(candidate),
                    .source_path = source_path.lexically_normal(),
                };
            }
        }
    }
    return nullopt;
}

std::vector<std::filesystem::path> ShaderSourceRegistry::roots() const {
    std::vector<std::filesystem::path> result;
    result.reserve(m_roots.size());
    for (const auto& source : m_roots) {
        result.push_back(source.root);
    }
    return result;
}

ShaderSourceRegistry generated_shader_source_registry() {
    ShaderSourceRegistry registry;
    auto sources = generated_shader_sources_text();
    while (!sources.empty()) {
        const auto separator = sources.find(';');
        auto entry = sources.substr(0, separator);
        add_shader_source_entry(registry, entry);
        if (separator == std::string_view::npos) {
            break;
        }
        sources.remove_prefix(separator + 1);
    }
    return registry;
}

Optional<ShaderStages>
shader_stage_from_path(const std::filesystem::path& path) {
    if (path.extension() == ".vert") {
        return ShaderStages::Vertex;
    }
    if (path.extension() == ".geom") {
        return ShaderStages::Geometry;
    }
    if (path.extension() == ".frag") {
        return ShaderStages::Fragment;
    }
    if (path.extension() == ".comp") {
        return ShaderStages::Compute;
    }
    return nullopt;
}

AssetLoadResult<Shader>
ShaderLoader::load(Reader& reader, const LoadContext& context) {
    auto logical_path = context.asset_path().path();
    auto stage = shader_stage_from_path(logical_path);
    if (logical_path.extension() != ".slang" && !stage) {
        return failure(shader_load_error(
            context,
            "Unknown shader extension: " + logical_path.string()
        ));
    }

    return std::make_unique<Shader>(Shader {
        .path = std::move(logical_path),
        .source = reader.as_string(),
    });
}

ShaderAssetSource::ShaderAssetSource() :
    m_registry(generated_shader_source_registry()) {}

ShaderAssetSource::ShaderAssetSource(std::filesystem::path root) :
    m_registry() {
    m_registry.add_root({}, std::move(root));
}

ShaderAssetSource::ShaderAssetSource(ShaderSourceRegistry registry) :
    m_registry(std::move(registry)) {}

std::string ShaderAssetSource::name() const {
    return "shader";
}

bool ShaderAssetSource::exists(const std::filesystem::path& path) const {
    return m_registry.resolve(path).has_value();
}

Result<Reader, std::string>
ShaderAssetSource::try_get_reader(const std::filesystem::path& path) const {
    auto source = m_registry.resolve(path);
    if (!source) {
        return failure(
            "Runtime Slang shader source is missing for '" + path.string() + "'"
        );
    }

    auto reader = Reader::from_file(source->source_path);
    if (!reader) {
        return failure(std::move(reader).error().message);
    }
    return std::move(reader).value();
}

} // namespace fei
