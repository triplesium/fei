#pragma once

#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/result.hpp"

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets {

using AssetImportSettings = std::map<std::string, std::string>;

struct AssetArtifact {
    std::string kind;
    std::filesystem::path path;
};

using AssetImportArtifacts = std::vector<AssetArtifact>;

struct AssetImportContext {
    AssetPath destination;
    AssetImportSettings settings;
    std::filesystem::path artifact_directory;
};

class AssetImporter {
  public:
    virtual ~AssetImporter() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::uint32_t version() const = 0;
    [[nodiscard]] virtual std::span<const std::string_view>
    extensions() const = 0;
    [[nodiscard]] virtual AssetImportSettings
    default_settings(const AssetPath& destination) const;
    [[nodiscard]] virtual Result<AssetImportArtifacts, std::string>
    import(const Reader& source, const AssetImportContext& context) const;
    [[nodiscard]] virtual Status<std::string>
    validate(const Reader& source, const AssetImportContext& context) const = 0;
};

class AssetImporterRegistry {
  public:
    AssetImporterRegistry() = default;
    ~AssetImporterRegistry() = default;
    AssetImporterRegistry(const AssetImporterRegistry&) = delete;
    AssetImporterRegistry& operator=(const AssetImporterRegistry&) = delete;
    AssetImporterRegistry(AssetImporterRegistry&&) noexcept = default;
    AssetImporterRegistry&
    operator=(AssetImporterRegistry&&) noexcept = default;

    bool add(std::unique_ptr<AssetImporter> importer);

    template<std::derived_from<AssetImporter> Importer, typename... Args>
    bool emplace(Args&&... args) {
        return add(std::make_unique<Importer>(std::forward<Args>(args)...));
    }

    [[nodiscard]] const AssetImporter*
    find_for(const std::filesystem::path& path) const;
    [[nodiscard]] const AssetImporter* find(std::string_view name) const;

  private:
    std::vector<std::unique_ptr<AssetImporter>> m_importers;
    std::unordered_map<std::string, const AssetImporter*> m_by_name;
    std::unordered_map<std::string, const AssetImporter*> m_by_extension;
};

} // namespace ets
