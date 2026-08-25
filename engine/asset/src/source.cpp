#include "asset/source.hpp"

#include "asset/embed.hpp"
#include "base/log.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace ets {

namespace {

std::filesystem::path canonical_root(std::filesystem::path root) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(root, error);
    if (error) {
        return root.lexically_normal();
    }
    auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

bool is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty()) {
        return candidate == root;
    }
    return !relative.is_absolute() && *relative.begin() != "..";
}

#ifdef __EMSCRIPTEN__
const FilesystemAssetSource& wasm_embedded_assets() {
    static const FilesystemAssetSource source {
        "embedded",
        "/entisium/embedded",
    };
    return source;
}
#endif

} // namespace

Reader AssetSource::get_reader(const std::filesystem::path& path) const {
    auto reader = try_get_reader(path);
    if (!reader) {
        fatal(
            "Failed to read asset from source '{}': {}",
            name(),
            reader.error()
        );
    }
    return std::move(*reader);
}

Result<std::vector<AssetEntry>, std::string> AssetSource::list(
    const std::filesystem::path& /*directory*/,
    bool /*recursive*/
) const {
    return failure("Asset source '" + name() + "' cannot be enumerated");
}

FilesystemAssetSource::FilesystemAssetSource(
    std::string name,
    std::filesystem::path root
) : m_name(std::move(name)), m_root(canonical_root(std::move(root))) {
    if (m_name.empty() || m_name.contains("://")) {
        fatal("Invalid filesystem asset source name: '{}'", m_name);
    }
}

std::string FilesystemAssetSource::name() const {
    return m_name;
}

Result<Reader, std::string>
FilesystemAssetSource::try_get_reader(const std::filesystem::path& path) const {
    auto resolved = resolve(path);
    if (!resolved) {
        return failure(std::move(resolved.error()));
    }
    auto reader = Reader::from_file(*resolved);
    if (!reader) {
        return failure(std::move(reader).error().message);
    }
    return std::move(*reader);
}

Result<std::filesystem::path, std::string>
FilesystemAssetSource::resolve(const std::filesystem::path& path) const {
    const auto normalized = path.lexically_normal();
    if (normalized.is_absolute() ||
        (!normalized.empty() && *normalized.begin() == "..")) {
        return failure(
            "Asset path escapes source '" + m_name + "': " + path.string()
        );
    }

    std::error_code error;
    auto candidate =
        std::filesystem::weakly_canonical(m_root / normalized, error);
    if (error) {
        return failure(
            "Failed to resolve path in asset source '" + m_name +
            "': " + error.message()
        );
    }
    if (!is_within(m_root, candidate)) {
        return failure(
            "Asset path escapes source '" + m_name + "': " + path.string()
        );
    }
    return candidate;
}

bool FilesystemAssetSource::exists(const std::filesystem::path& path) const {
    auto resolved = resolve(path);
    if (!resolved) {
        return false;
    }
    std::error_code error;
    return std::filesystem::exists(*resolved, error) && !error;
}

Result<std::vector<AssetEntry>, std::string> FilesystemAssetSource::list(
    const std::filesystem::path& directory,
    bool recursive
) const {
    auto resolved = resolve(directory);
    if (!resolved) {
        return failure(std::move(resolved.error()));
    }

    std::error_code error;
    if (!std::filesystem::is_directory(*resolved, error) || error) {
        return failure(
            "Asset directory does not exist in source '" + m_name +
            "': " + directory.string()
        );
    }

    std::vector<AssetEntry> entries;
    const auto append_entry = [&](
                                  const std::filesystem::directory_entry& item
                              ) {
        std::error_code item_error;
        if (item.is_symlink(item_error) || item_error) {
            return;
        }

        const bool is_directory = item.is_directory(item_error);
        if (item_error) {
            return;
        }
        const bool is_file = item.is_regular_file(item_error);
        if (item_error || (!is_directory && !is_file)) {
            return;
        }

        auto relative = item.path().lexically_relative(m_root);
        auto modified_at = item.last_write_time(item_error);
        if (item_error) {
            modified_at = {};
            item_error.clear();
        }
        const auto size = is_file ? item.file_size(item_error) : 0;
        entries.push_back(
            AssetEntry {
                .path =
                    AssetPath(relative.generic_string()).with_source(m_name),
                .kind = is_directory ? AssetEntryKind::Directory :
                                       AssetEntryKind::File,
                .size = item_error ? 0 : size,
                .modified_at = modified_at,
            }
        );
    };

    constexpr auto options =
        std::filesystem::directory_options::skip_permission_denied;
    if (recursive) {
        std::filesystem::recursive_directory_iterator iterator(
            *resolved,
            options,
            error
        );
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            append_entry(*iterator);
            iterator.increment(error);
        }
    } else {
        std::filesystem::directory_iterator iterator(*resolved, options, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            append_entry(*iterator);
            iterator.increment(error);
        }
    }
    if (error) {
        return failure(
            "Failed to enumerate asset source '" + m_name +
            "': " + error.message()
        );
    }

    std::ranges::sort(entries, {}, [](const AssetEntry& entry) {
        return entry.path.as_string();
    });
    return entries;
}

std::string EmbeddedAssetSource::name() const {
    return "embedded";
}

bool EmbeddedAssetSource::exists(const std::filesystem::path& path) const {
    const bool registered = EmbeddedAssets::has(path.generic_string());
#ifdef __EMSCRIPTEN__
    return registered || wasm_embedded_assets().exists(path);
#else
    return registered;
#endif
}

Result<Reader, std::string>
EmbeddedAssetSource::try_get_reader(const std::filesystem::path& path) const {
    const auto name = path.generic_string();
    if (EmbeddedAssets::has(name)) {
        return EmbeddedAssets::get(name).reader();
    }
#ifdef __EMSCRIPTEN__
    return wasm_embedded_assets().try_get_reader(path);
#else
    return failure("No embedded asset found with name: " + name);
#endif
}

} // namespace ets
