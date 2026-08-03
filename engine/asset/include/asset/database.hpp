#pragma once

#include "asset/importer.hpp"
#include "asset/path.hpp"
#include "asset/uuid.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei {

inline constexpr std::string_view native_asset_importer_name = "native";

struct AssetMetadata {
    AssetUuid id;
    std::string importer;
    AssetImportSettings settings;
};

struct AssetImportRecord {
    std::uint32_t importer_version {1};
    std::string source_hash;
    std::string settings_hash;
    AssetImportArtifacts artifacts;
};

enum class AssetImportState : std::uint8_t {
    Unimported,
    Imported,
    Failed,
};

enum class AssetImportErrorKind : std::uint8_t {
    Source,
    UnsupportedType,
    InvalidDestination,
    DestinationExists,
    AlreadyImported,
    Validation,
    Io,
    Metadata,
};

struct AssetImportError {
    AssetImportErrorKind kind {AssetImportErrorKind::Io};
    AssetPath destination;
    std::string message;
};

struct AssetImportRequest {
    std::filesystem::path source_file;
    AssetPath destination;
    AssetImportSettings settings;
};

struct AssetImportResult {
    AssetPath path;
    AssetMetadata metadata;
    AssetImportRecord record;
    bool copied {false};
};

struct AssetImportReport {
    std::vector<AssetImportResult> imported;
    std::vector<AssetImportError> failed;
};

struct AssetMoveResult {
    AssetPath source;
    AssetPath destination;
    Optional<AssetUuid> id;
};

struct AssetDeleteResult {
    AssetPath path;
    Optional<AssetUuid> id;
};

class AssetDatabase {
  public:
    explicit AssetDatabase(
        std::filesystem::path project_asset_root,
        std::filesystem::path import_cache_root = {}
    );

    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }
    [[nodiscard]] const std::filesystem::path& import_cache_root() const {
        return m_import_cache_root;
    }
    [[nodiscard]] Result<std::filesystem::path, std::string>
    resolve(const AssetPath& path) const;
    [[nodiscard]] std::filesystem::path
    metadata_path(const AssetPath& path) const;
    [[nodiscard]] std::filesystem::path import_record_path(AssetUuid id) const;
    [[nodiscard]] Optional<std::filesystem::path>
    artifact_path(const AssetPath& path, std::string_view kind) const;

    [[nodiscard]] const AssetMetadata* metadata(const AssetPath& path) const;
    [[nodiscard]] const AssetImportRecord*
    import_record(const AssetPath& path) const;
    [[nodiscard]] Optional<AssetPath> path(AssetUuid id) const;
    [[nodiscard]] std::vector<std::pair<AssetUuid, AssetPath>>
    registered_assets() const;
    [[nodiscard]] AssetImportState state(const AssetPath& path) const;
    [[nodiscard]] Optional<std::string> error(const AssetPath& path) const;

    Status<std::string> scan();
    Status<std::string>
    register_metadata(const AssetPath& path, AssetMetadata metadata);
    Status<std::string>
    register_import_record(AssetUuid id, AssetImportRecord record);
    [[nodiscard]] Result<AssetMetadata, std::string>
    ensure_native_asset(const AssetPath& path);
    [[nodiscard]] bool is_import_current(
        const AssetPath& path,
        const AssetImporter& importer,
        const Reader& source
    ) const;
    void record_failure(const AssetPath& path, std::string message);
    void clear_failure(const AssetPath& path);
    [[nodiscard]] Result<AssetMoveResult, std::string>
    move_asset(const AssetPath& source, const AssetPath& destination);
    [[nodiscard]] Result<AssetPath, std::string>
    copy_asset_file(const AssetPath& source, const AssetPath& destination);
    Status<std::string> create_directory(const AssetPath& path);
    [[nodiscard]] Result<AssetDeleteResult, std::string>
    delete_asset(const AssetPath& path);
    Status<std::string> delete_empty_directory(const AssetPath& path);

  private:
    std::filesystem::path m_root;
    std::filesystem::path m_import_cache_root;
    std::unordered_map<AssetPath, AssetMetadata> m_by_path;
    std::unordered_map<AssetUuid, AssetPath> m_by_id;
    std::unordered_map<AssetUuid, AssetImportRecord> m_import_records;
    std::unordered_map<AssetPath, std::string> m_errors;
};

Result<AssetMetadata, std::string>
read_asset_metadata(const std::filesystem::path& path);
Status<std::string> write_asset_metadata(
    const std::filesystem::path& path,
    const AssetMetadata& metadata
);
Result<AssetImportRecord, std::string>
read_asset_import_record(const std::filesystem::path& path);
Status<std::string> write_asset_import_record(
    const std::filesystem::path& path,
    const AssetImportRecord& record
);

Result<AssetImportResult, AssetImportError> import_asset(
    const AssetImportRequest& request,
    const AssetImporterRegistry& importers,
    AssetDatabase& database
);
Result<AssetImportReport, std::string> import_pending_assets(
    const AssetImporterRegistry& importers,
    AssetDatabase& database
);

} // namespace fei
