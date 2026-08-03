#include "asset/database.hpp"

#include "asset/io.hpp"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <yaml-cpp/yaml.h> // IWYU pragma: keep

namespace fei {

namespace {

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

std::filesystem::path canonical_root(std::filesystem::path root) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(root, error);
    if (error) {
        return root.lexically_normal();
    }
    auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

std::string content_hash(const Reader& reader) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte :
         std::span<const std::byte>(reader.data(), reader.size())) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return std::format("{:016x}", hash);
}

std::string settings_hash(const AssetImportSettings& settings) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto append = [&hash](std::string_view value) {
        for (const auto byte : value) {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xffU;
        hash *= 1099511628211ULL;
    };
    for (const auto& [name, value] : settings) {
        append(name);
        append(value);
    }
    return std::format("{:016x}", hash);
}

AssetImportError import_error(
    AssetImportErrorKind kind,
    const AssetPath& destination,
    std::string message
) {
    return AssetImportError {
        .kind = kind,
        .destination = destination,
        .message = std::move(message),
    };
}

Status<std::string> remove_cache_directory(
    const std::filesystem::path& cache_root,
    const std::filesystem::path& directory
) {
    const auto normalized_root = canonical_root(cache_root);
    auto normalized_directory = std::filesystem::absolute(directory);
    normalized_directory = normalized_directory.lexically_normal();
    if (normalized_directory == normalized_root ||
        !is_within(normalized_root, normalized_directory)) {
        return failure(
            "Refusing to remove path outside the import cache: " +
            directory.string()
        );
    }
    std::error_code error;
    std::filesystem::remove_all(normalized_directory, error);
    if (error) {
        return failure(
            "Failed to remove import cache directory: " + error.message()
        );
    }
    return {};
}

Status<std::string> validate_artifacts(
    AssetImportArtifacts& artifacts,
    const std::filesystem::path& staging_directory
) {
    std::unordered_set<std::string> kinds;
    for (auto& artifact : artifacts) {
        artifact.path = artifact.path.lexically_normal();
        if (artifact.kind.empty() || !kinds.insert(artifact.kind).second) {
            return failure(
                std::string(
                    "Imported artifact kinds must be non-empty and unique"
                )
            );
        }
        if (artifact.path.empty() || artifact.path.is_absolute() ||
            *artifact.path.begin() == "..") {
            return failure(
                "Imported artifact path must be relative: " +
                artifact.path.string()
            );
        }
        const auto file =
            (staging_directory / artifact.path).lexically_normal();
        if (!is_within(staging_directory, file)) {
            return failure(
                "Imported artifact escapes its staging directory: " +
                artifact.path.string()
            );
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error) || error) {
            return failure(
                "Imported artifact was not written: " + file.string()
            );
        }
    }
    return {};
}

} // namespace

AssetDatabase::AssetDatabase(
    std::filesystem::path project_asset_root,
    std::filesystem::path import_cache_root
) : m_root(canonical_root(std::move(project_asset_root))) {
    if (import_cache_root.empty()) {
        import_cache_root = m_root.parent_path() / ".fei" / "imported";
    }
    m_import_cache_root = canonical_root(std::move(import_cache_root));
}

Result<std::filesystem::path, std::string>
AssetDatabase::resolve(const AssetPath& path) const {
    auto normalized = path.normalized();
    if (!normalized.source()) {
        normalized = normalized.with_source("project");
    }
    if (*normalized.source() != "project" || normalized.is_unapproved()) {
        return failure(
            "Asset database only accepts safe project:// paths: " +
            path.as_string()
        );
    }

    std::error_code error;
    auto candidate =
        std::filesystem::weakly_canonical(m_root / normalized.path(), error);
    if (error) {
        return failure("Failed to resolve asset path: " + error.message());
    }
    if (!is_within(m_root, candidate)) {
        return failure("Asset path escapes project root: " + path.as_string());
    }
    return candidate;
}

std::filesystem::path
AssetDatabase::metadata_path(const AssetPath& path) const {
    auto resolved = resolve(path);
    if (!resolved) {
        return {};
    }
    auto metadata = resolved->string();
    metadata += ".meta";
    return metadata;
}

std::filesystem::path AssetDatabase::import_record_path(AssetUuid id) const {
    return m_import_cache_root / id.as_string() / "import.yaml";
}

Optional<std::filesystem::path> AssetDatabase::artifact_path(
    const AssetPath& path,
    std::string_view kind
) const {
    const auto* asset_metadata = metadata(path);
    const auto* record = import_record(path);
    if (!asset_metadata || !record) {
        return nullopt;
    }
    for (const auto& artifact : record->artifacts) {
        if (artifact.kind == kind) {
            const auto cache_directory =
                import_record_path(asset_metadata->id).parent_path();
            auto candidate =
                (cache_directory / artifact.path).lexically_normal();
            if (is_within(cache_directory, candidate)) {
                return candidate;
            }
            return nullopt;
        }
    }
    return nullopt;
}

const AssetMetadata* AssetDatabase::metadata(const AssetPath& path) const {
    auto normalized = path.normalized();
    if (!normalized.source()) {
        normalized = normalized.with_source("project");
    }
    const auto metadata = m_by_path.find(normalized);
    return metadata == m_by_path.end() ? nullptr : &metadata->second;
}

const AssetImportRecord*
AssetDatabase::import_record(const AssetPath& path) const {
    const auto* asset_metadata = metadata(path);
    if (!asset_metadata) {
        return nullptr;
    }
    const auto record = m_import_records.find(asset_metadata->id);
    return record == m_import_records.end() ? nullptr : &record->second;
}

Optional<AssetPath> AssetDatabase::path(AssetUuid id) const {
    const auto path = m_by_id.find(id);
    if (path == m_by_id.end()) {
        return nullopt;
    }
    return path->second;
}

AssetImportState AssetDatabase::state(const AssetPath& path) const {
    auto normalized = path.normalized();
    if (!normalized.source()) {
        normalized = normalized.with_source("project");
    }
    if (m_errors.contains(normalized)) {
        return AssetImportState::Failed;
    }
    const auto metadata = m_by_path.find(normalized);
    if (metadata != m_by_path.end() &&
        m_import_records.contains(metadata->second.id)) {
        return AssetImportState::Imported;
    }
    return AssetImportState::Unimported;
}

Optional<std::string> AssetDatabase::error(const AssetPath& path) const {
    auto normalized = path.normalized();
    if (!normalized.source()) {
        normalized = normalized.with_source("project");
    }
    const auto error = m_errors.find(normalized);
    return error == m_errors.end() ? Optional<std::string> {} : error->second;
}

Status<std::string> AssetDatabase::scan() {
    m_by_path.clear();
    m_by_id.clear();
    m_import_records.clear();
    m_errors.clear();

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        m_root,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    std::string first_error;
    while (!error && iterator != end) {
        const auto& entry = *iterator;
        if (entry.is_regular_file(error) && !error &&
            entry.path().extension() == ".meta") {
            const auto& metadata_file = entry.path();
            const auto source_file =
                metadata_file.parent_path() / metadata_file.stem();
            const auto relative = source_file.lexically_relative(m_root);
            const auto path =
                AssetPath(relative.generic_string()).with_source("project");
            auto metadata = read_asset_metadata(metadata_file);
            if (!metadata) {
                record_failure(path, metadata.error());
                if (first_error.empty()) {
                    first_error = metadata.error();
                }
            } else {
                auto status = register_metadata(path, std::move(*metadata));
                if (!status) {
                    record_failure(path, status.error());
                    if (first_error.empty()) {
                        first_error = status.error();
                    }
                }
            }
        }
        iterator.increment(error);
    }
    if (error) {
        return failure("Failed to scan asset metadata: " + error.message());
    }

    for (const auto& [path, metadata] : m_by_path) {
        const auto record_file = import_record_path(metadata.id);
        if (!std::filesystem::exists(record_file, error)) {
            if (error && first_error.empty()) {
                first_error =
                    "Failed to inspect asset import record: " + error.message();
                record_failure(path, first_error);
            }
            error.clear();
            continue;
        }
        auto record = read_asset_import_record(record_file);
        if (!record) {
            record_failure(path, record.error());
            if (first_error.empty()) {
                first_error = record.error();
            }
            continue;
        }
        m_import_records.insert_or_assign(metadata.id, std::move(*record));
    }
    if (!first_error.empty()) {
        return failure(std::move(first_error));
    }
    return {};
}

Status<std::string>
AssetDatabase::register_import_record(AssetUuid id, AssetImportRecord record) {
    if (!m_by_id.contains(id)) {
        return failure(
            "Cannot register an import record for unknown asset " +
            id.as_string()
        );
    }
    m_import_records.insert_or_assign(id, std::move(record));
    clear_failure(m_by_id.at(id));
    return {};
}

bool AssetDatabase::is_import_current(
    const AssetPath& path,
    const AssetImporter& importer,
    const Reader& source
) const {
    const auto* asset_metadata = metadata(path);
    const auto* record = import_record(path);
    return asset_metadata && record &&
           asset_metadata->importer == importer.name() &&
           record->importer_version == importer.version() &&
           record->source_hash == content_hash(source) &&
           record->settings_hash == settings_hash(asset_metadata->settings);
}

Status<std::string> AssetDatabase::register_metadata(
    const AssetPath& path,
    AssetMetadata metadata
) {
    auto normalized = path.normalized();
    if (!normalized.source()) {
        normalized = normalized.with_source("project");
    }
    if (metadata.id.is_nil()) {
        return failure("Asset metadata contains a nil ID: " + path.as_string());
    }
    const auto existing_id = m_by_id.find(metadata.id);
    if (existing_id != m_by_id.end() && existing_id->second != normalized) {
        return failure(
            "Duplicate asset ID " + metadata.id.as_string() + " for " +
            normalized.as_string() + " and " + existing_id->second.as_string()
        );
    }

    if (const auto existing = m_by_path.find(normalized);
        existing != m_by_path.end()) {
        m_by_id.erase(existing->second.id);
        if (existing->second.id != metadata.id) {
            m_import_records.erase(existing->second.id);
        }
    }
    m_by_id.insert_or_assign(metadata.id, normalized);
    m_by_path.insert_or_assign(normalized, std::move(metadata));
    clear_failure(normalized);
    return {};
}

void AssetDatabase::record_failure(const AssetPath& path, std::string message) {
    auto normalized = path.normalized();
    if (!normalized.source()) {
        normalized = normalized.with_source("project");
    }
    m_errors.insert_or_assign(std::move(normalized), std::move(message));
}

void AssetDatabase::clear_failure(const AssetPath& path) {
    auto normalized = path.normalized();
    if (!normalized.source()) {
        normalized = normalized.with_source("project");
    }
    m_errors.erase(normalized);
}

Result<AssetMetadata, std::string>
read_asset_metadata(const std::filesystem::path& path) {
    try {
        const auto document = YAML::LoadFile(path.string());
        if (!document.IsMap() || !document["id"].IsScalar() ||
            !document["importer"].IsScalar()) {
            return failure("Invalid asset metadata: " + path.string());
        }

        auto id = AssetUuid::parse(document["id"].as<std::string>());
        if (!id) {
            return failure(std::move(id.error()));
        }
        AssetMetadata metadata {
            .id = *id,
            .importer = document["importer"].as<std::string>(),
            .settings = {},
        };
        const auto settings = document["settings"];
        if (settings) {
            if (!settings.IsMap()) {
                return failure(
                    "Asset metadata settings must be a mapping: " +
                    path.string()
                );
            }
            for (const auto& setting : settings) {
                if (!setting.first.IsScalar() || !setting.second.IsScalar()) {
                    return failure(
                        "Asset metadata settings must be scalar values: " +
                        path.string()
                    );
                }
                metadata.settings.emplace(
                    setting.first.as<std::string>(),
                    setting.second.as<std::string>()
                );
            }
        }
        return metadata;
    } catch (const YAML::Exception& yaml_error) {
        return failure(
            "Failed to read asset metadata '" + path.string() +
            "': " + yaml_error.what()
        );
    }
}

Status<std::string> write_asset_metadata(
    const std::filesystem::path& path,
    const AssetMetadata& metadata
) {
    YAML::Emitter output;
    output << YAML::BeginMap;
    output << YAML::Key << "id" << YAML::Value << metadata.id.as_string();
    output << YAML::Key << "importer" << YAML::Value << metadata.importer;
    output << YAML::Key << "settings" << YAML::Value << YAML::BeginMap;
    for (const auto& [name, value] : metadata.settings) {
        output << YAML::Key << name << YAML::Value << value;
    }
    output << YAML::EndMap << YAML::EndMap;
    if (!output.good()) {
        return failure(
            "Failed to serialize asset metadata: " + output.GetLastError()
        );
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return failure(
                "Failed to create asset metadata: " + temporary.string()
            );
        }
        stream << output.c_str() << '\n';
        if (!stream) {
            return failure(
                "Failed to write asset metadata: " + temporary.string()
            );
        }
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return failure("Failed to replace asset metadata: " + error.message());
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        const auto message = error.message();
        std::filesystem::remove(temporary, error);
        return failure("Failed to publish asset metadata: " + message);
    }
    return {};
}

Result<AssetImportRecord, std::string>
read_asset_import_record(const std::filesystem::path& path) {
    try {
        const auto document = YAML::LoadFile(path.string());
        if (!document.IsMap() || !document["importer_version"].IsScalar() ||
            !document["source_hash"].IsScalar() ||
            !document["settings_hash"].IsScalar()) {
            return failure("Invalid asset import record: " + path.string());
        }

        AssetImportRecord record {
            .importer_version =
                document["importer_version"].as<std::uint32_t>(),
            .source_hash = document["source_hash"].as<std::string>(),
            .settings_hash = document["settings_hash"].as<std::string>(),
            .artifacts = {},
        };
        const auto artifacts = document["artifacts"];
        if (artifacts) {
            if (!artifacts.IsSequence()) {
                return failure(
                    "Asset import record artifacts must be a sequence: " +
                    path.string()
                );
            }
            for (const auto& artifact : artifacts) {
                AssetArtifact imported_artifact;
                if (artifact.IsScalar()) {
                    imported_artifact.path =
                        std::filesystem::path(artifact.as<std::string>());
                } else if (
                    artifact.IsMap() && artifact["kind"].IsScalar() &&
                    artifact["path"].IsScalar()
                ) {
                    imported_artifact.kind = artifact["kind"].as<std::string>();
                    imported_artifact.path = std::filesystem::path(
                        artifact["path"].as<std::string>()
                    );
                } else {
                    return failure(
                        "Asset import record artifacts must contain kind and "
                        "path: " +
                        path.string()
                    );
                }
                const auto& artifact_path = imported_artifact.path;
                if (artifact_path.empty() || artifact_path.is_absolute() ||
                    *artifact_path.lexically_normal().begin() == "..") {
                    return failure(
                        "Asset import artifact must be a relative path: " +
                        path.string()
                    );
                }
                record.artifacts.push_back(std::move(imported_artifact));
            }
        }
        return record;
    } catch (const YAML::Exception& yaml_error) {
        return failure(
            "Failed to read asset import record '" + path.string() +
            "': " + yaml_error.what()
        );
    }
}

Status<std::string> write_asset_import_record(
    const std::filesystem::path& path,
    const AssetImportRecord& record
) {
    YAML::Emitter output;
    output << YAML::BeginMap;
    output << YAML::Key << "importer_version" << YAML::Value
           << record.importer_version;
    output << YAML::Key << "source_hash" << YAML::Value << record.source_hash;
    output << YAML::Key << "settings_hash" << YAML::Value
           << record.settings_hash;
    output << YAML::Key << "artifacts" << YAML::Value << YAML::BeginSeq;
    for (const auto& artifact : record.artifacts) {
        output << YAML::BeginMap;
        output << YAML::Key << "kind" << YAML::Value << artifact.kind;
        output << YAML::Key << "path" << YAML::Value
               << artifact.path.generic_string();
        output << YAML::EndMap;
    }
    output << YAML::EndSeq << YAML::EndMap;
    if (!output.good()) {
        return failure(
            "Failed to serialize asset import record: " + output.GetLastError()
        );
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return failure(
            "Failed to create asset import cache directory: " + error.message()
        );
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return failure(
                "Failed to create asset import record: " + temporary.string()
            );
        }
        stream << output.c_str() << '\n';
        if (!stream) {
            return failure(
                "Failed to write asset import record: " + temporary.string()
            );
        }
    }

    std::filesystem::remove(path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return failure(
            "Failed to replace asset import record: " + error.message()
        );
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        const auto message = error.message();
        std::filesystem::remove(temporary, error);
        return failure("Failed to publish asset import record: " + message);
    }
    return {};
}

Result<AssetImportResult, AssetImportError> import_asset(
    const AssetImportRequest& request,
    const AssetImporterRegistry& importers,
    AssetDatabase& database
) {
    auto destination = request.destination.normalized();
    if (!destination.source()) {
        destination = destination.with_source("project");
    }
    const auto fail = [&](AssetImportErrorKind kind, std::string message) {
        database.record_failure(destination, message);
        return failure(import_error(kind, destination, std::move(message)));
    };

    auto destination_file = database.resolve(destination);
    if (!destination_file || destination.path().empty()) {
        return fail(
            AssetImportErrorKind::InvalidDestination,
            destination_file ? "Import destination must name a file" :
                               destination_file.error()
        );
    }

    std::error_code filesystem_error;
    auto source_file = std::filesystem::weakly_canonical(
        request.source_file,
        filesystem_error
    );
    if (filesystem_error ||
        !std::filesystem::is_regular_file(source_file, filesystem_error)) {
        return fail(
            AssetImportErrorKind::Source,
            "Import source file does not exist: " + request.source_file.string()
        );
    }

    const auto* importer = importers.find_for(source_file);
    const auto* destination_importer = importers.find_for(destination.path());
    if (!importer || importer != destination_importer) {
        return fail(
            AssetImportErrorKind::UnsupportedType,
            "No compatible importer for " + source_file.extension().string()
        );
    }
    const auto* existing_metadata_ptr = database.metadata(destination);
    if (existing_metadata_ptr &&
        existing_metadata_ptr->importer != importer->name()) {
        return fail(
            AssetImportErrorKind::Metadata,
            "Asset metadata selects importer '" +
                existing_metadata_ptr->importer + "', not '" +
                std::string(importer->name()) + "'"
        );
    }
    const Optional<AssetMetadata> existing_metadata =
        existing_metadata_ptr ?
            Optional<AssetMetadata> {
                *existing_metadata_ptr,
            } :
            nullopt;

    bool in_place = false;
    if (std::filesystem::exists(*destination_file, filesystem_error) &&
        !filesystem_error) {
        in_place = std::filesystem::equivalent(
            source_file,
            *destination_file,
            filesystem_error
        );
        if (filesystem_error || !in_place) {
            auto error = import_error(
                AssetImportErrorKind::DestinationExists,
                destination,
                "Import destination already exists: " +
                    destination_file->string()
            );
            if (!existing_metadata_ptr) {
                database.record_failure(destination, error.message);
            }
            return failure(std::move(error));
        }
    }

    auto reader = Reader::from_file(source_file);
    if (!reader) {
        return fail(AssetImportErrorKind::Source, reader.error().message);
    }
    auto settings = existing_metadata ? existing_metadata->settings :
                                        importer->default_settings(destination);
    for (const auto& [name, value] : request.settings) {
        settings.insert_or_assign(name, value);
    }
    const auto metadata_file = database.metadata_path(destination);
    if (!existing_metadata_ptr &&
        std::filesystem::exists(metadata_file, filesystem_error) &&
        !filesystem_error) {
        return fail(
            AssetImportErrorKind::Metadata,
            "Asset metadata already exists: " + metadata_file.string()
        );
    }

    AssetMetadata metadata = existing_metadata ?
                                 *existing_metadata :
                                 AssetMetadata {
                                     .id = AssetUuid::random(),
                                     .importer = std::string(importer->name()),
                                     .settings = {},
                                 };
    metadata.settings = std::move(settings);

    const auto record_file = database.import_record_path(metadata.id);
    const auto cache_directory = record_file.parent_path();
    auto staging_directory = cache_directory;
    staging_directory += ".tmp";
    auto backup_directory = cache_directory;
    backup_directory += ".bak";

    if (auto cleanup = remove_cache_directory(
            database.import_cache_root(),
            staging_directory
        );
        !cleanup) {
        return fail(AssetImportErrorKind::Io, cleanup.error());
    }
    std::filesystem::create_directories(staging_directory, filesystem_error);
    if (filesystem_error) {
        return fail(
            AssetImportErrorKind::Io,
            "Failed to create artifact staging directory: " +
                filesystem_error.message()
        );
    }

    const AssetImportContext context {
        .destination = destination,
        .settings = metadata.settings,
        .artifact_directory = staging_directory,
    };
    auto artifacts = importer->import(*reader, context);
    if (!artifacts) {
        remove_cache_directory(database.import_cache_root(), staging_directory);
        return fail(AssetImportErrorKind::Validation, artifacts.error());
    }
    if (auto artifact_status =
            validate_artifacts(*artifacts, staging_directory);
        !artifact_status) {
        remove_cache_directory(database.import_cache_root(), staging_directory);
        return fail(AssetImportErrorKind::Validation, artifact_status.error());
    }

    AssetImportRecord record {
        .importer_version = importer->version(),
        .source_hash = content_hash(*reader),
        .settings_hash = settings_hash(metadata.settings),
        .artifacts = std::move(*artifacts),
    };
    auto record_status =
        write_asset_import_record(staging_directory / "import.yaml", record);
    if (!record_status) {
        remove_cache_directory(database.import_cache_root(), staging_directory);
        return fail(AssetImportErrorKind::Metadata, record_status.error());
    }

    std::filesystem::create_directories(
        destination_file->parent_path(),
        filesystem_error
    );
    if (filesystem_error) {
        remove_cache_directory(database.import_cache_root(), staging_directory);
        return fail(
            AssetImportErrorKind::Io,
            "Failed to create import directory: " + filesystem_error.message()
        );
    }

    bool copied = false;
    if (!in_place) {
        copied = std::filesystem::copy_file(
            source_file,
            *destination_file,
            std::filesystem::copy_options::none,
            filesystem_error
        );
        if (!copied || filesystem_error) {
            remove_cache_directory(
                database.import_cache_root(),
                staging_directory
            );
            return fail(
                AssetImportErrorKind::Io,
                "Failed to copy imported asset: " + filesystem_error.message()
            );
        }
    }

    if (std::filesystem::exists(backup_directory, filesystem_error) &&
        !filesystem_error) {
        auto cleanup = remove_cache_directory(
            database.import_cache_root(),
            backup_directory
        );
        if (!cleanup) {
            remove_cache_directory(
                database.import_cache_root(),
                staging_directory
            );
            if (copied) {
                std::filesystem::remove(*destination_file, filesystem_error);
            }
            return fail(AssetImportErrorKind::Io, cleanup.error());
        }
    }
    filesystem_error.clear();
    const bool had_cache =
        std::filesystem::exists(cache_directory, filesystem_error) &&
        !filesystem_error;
    if (had_cache) {
        std::filesystem::rename(
            cache_directory,
            backup_directory,
            filesystem_error
        );
    }
    if (!filesystem_error) {
        std::filesystem::rename(
            staging_directory,
            cache_directory,
            filesystem_error
        );
    }
    if (filesystem_error) {
        const auto message = filesystem_error.message();
        if (had_cache && !std::filesystem::exists(cache_directory)) {
            filesystem_error.clear();
            std::filesystem::rename(
                backup_directory,
                cache_directory,
                filesystem_error
            );
        }
        remove_cache_directory(database.import_cache_root(), staging_directory);
        if (copied) {
            std::filesystem::remove(*destination_file, filesystem_error);
        }
        return fail(
            AssetImportErrorKind::Io,
            "Failed to publish imported artifacts: " + message
        );
    }

    const auto rollback_cache = [&] {
        remove_cache_directory(database.import_cache_root(), cache_directory);
        if (had_cache) {
            std::error_code rollback_error;
            std::filesystem::rename(
                backup_directory,
                cache_directory,
                rollback_error
            );
        }
    };

    auto metadata_status = write_asset_metadata(metadata_file, metadata);
    if (!metadata_status) {
        rollback_cache();
        if (copied) {
            std::filesystem::remove(*destination_file, filesystem_error);
        }
        return fail(AssetImportErrorKind::Metadata, metadata_status.error());
    }
    auto register_status = database.register_metadata(destination, metadata);
    if (!register_status) {
        rollback_cache();
        if (existing_metadata) {
            write_asset_metadata(metadata_file, *existing_metadata);
        } else {
            std::filesystem::remove(metadata_file, filesystem_error);
        }
        if (copied) {
            std::filesystem::remove(*destination_file, filesystem_error);
        }
        return fail(AssetImportErrorKind::Metadata, register_status.error());
    }
    register_status = database.register_import_record(metadata.id, record);
    if (!register_status) {
        rollback_cache();
        return fail(AssetImportErrorKind::Metadata, register_status.error());
    }

    remove_cache_directory(database.import_cache_root(), backup_directory);

    database.clear_failure(destination);
    return AssetImportResult {
        .path = std::move(destination),
        .metadata = std::move(metadata),
        .record = std::move(record),
        .copied = copied,
    };
}

Result<AssetImportReport, std::string> import_pending_assets(
    const AssetImporterRegistry& importers,
    AssetDatabase& database
) {
    AssetImportReport report;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        database.root(),
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto& entry = *iterator;
        if (entry.is_regular_file(error) && !error &&
            entry.path().extension() != ".meta") {
            const auto* importer = importers.find_for(entry.path());
            if (!importer) {
                iterator.increment(error);
                continue;
            }
            const auto relative =
                entry.path().lexically_relative(database.root());
            const auto path =
                AssetPath(relative.generic_string()).with_source("project");
            if (database.state(path) != AssetImportState::Failed) {
                auto source = Reader::from_file(entry.path());
                if (!source) {
                    database.record_failure(path, source.error().message);
                    report.failed.push_back(import_error(
                        AssetImportErrorKind::Source,
                        path,
                        source.error().message
                    ));
                    iterator.increment(error);
                    continue;
                }
                if (database.is_import_current(path, *importer, *source)) {
                    iterator.increment(error);
                    continue;
                }
                auto result = import_asset(
                    AssetImportRequest {
                        .source_file = entry.path(),
                        .destination = path,
                        .settings = {},
                    },
                    importers,
                    database
                );
                if (result) {
                    report.imported.push_back(std::move(*result));
                } else {
                    report.failed.push_back(std::move(result.error()));
                }
            }
        }
        iterator.increment(error);
    }
    if (error) {
        return failure("Failed to discover project assets: " + error.message());
    }
    return report;
}

} // namespace fei
