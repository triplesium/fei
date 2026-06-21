#include "asset/source.hpp"

#include "asset/embed.hpp"
#include "base/log.hpp"

#include <utility>

namespace fei {

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

DefaultAssetSource::DefaultAssetSource() {
#ifdef FEI_ASSETS_PATH
    m_base_path = FEI_ASSETS_PATH;
#else
    m_base_path = std::filesystem::current_path();
#endif
}

std::string DefaultAssetSource::name() const {
    return "default";
}

bool DefaultAssetSource::exists(const std::filesystem::path& path) const {
    return std::filesystem::exists(m_base_path / path);
}

Result<Reader, std::string>
DefaultAssetSource::try_get_reader(const std::filesystem::path& path) const {
    auto reader = Reader::from_file(m_base_path / path);
    if (!reader) {
        return failure(std::move(reader).error().message);
    }
    return std::move(*reader);
}

std::string EmbededAssetSource::name() const {
    return "embeded";
}

bool EmbededAssetSource::exists(const std::filesystem::path& path) const {
    return EmbededAssets::has(path.string());
}

Result<Reader, std::string>
EmbededAssetSource::try_get_reader(const std::filesystem::path& path) const {
    if (!EmbededAssets::has(path.string())) {
        return failure("No embedded asset found with name: " + path.string());
    }
    return EmbededAssets::get(path.string()).reader();
}

} // namespace fei
