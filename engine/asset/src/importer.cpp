#include "asset/importer.hpp"

#include <algorithm>
#include <cctype>

namespace fei {

namespace {

std::string normalized_extension(std::string extension) {
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    return extension;
}

} // namespace

AssetImportSettings
AssetImporter::default_settings(const AssetPath& /*destination*/) const {
    return {};
}

Result<AssetImportArtifacts, std::string> AssetImporter::import(
    const Reader& source,
    const AssetImportContext& context
) const {
    auto status = validate(source, context);
    if (!status) {
        return failure(std::move(status.error()));
    }
    return AssetImportArtifacts {};
}

bool AssetImporterRegistry::add(std::unique_ptr<AssetImporter> importer) {
    if (!importer || importer->name().empty() ||
        m_by_name.contains(std::string(importer->name()))) {
        return false;
    }
    for (const auto extension : importer->extensions()) {
        const auto normalized = normalized_extension(std::string(extension));
        if (normalized.empty() || m_by_extension.contains(normalized)) {
            return false;
        }
    }

    const auto* importer_ptr = importer.get();
    m_by_name.emplace(std::string(importer->name()), importer_ptr);
    for (const auto extension : importer->extensions()) {
        m_by_extension.emplace(
            normalized_extension(std::string(extension)),
            importer_ptr
        );
    }
    m_importers.push_back(std::move(importer));
    return true;
}

const AssetImporter*
AssetImporterRegistry::find_for(const std::filesystem::path& path) const {
    const auto extension = normalized_extension(path.extension().string());
    const auto importer = m_by_extension.find(extension);
    return importer == m_by_extension.end() ? nullptr : importer->second;
}

const AssetImporter* AssetImporterRegistry::find(std::string_view name) const {
    const auto importer = m_by_name.find(std::string(name));
    return importer == m_by_name.end() ? nullptr : importer->second;
}

} // namespace fei
