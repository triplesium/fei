#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/io.hpp"
#include "asset/uuid.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

using namespace fei;

namespace {

class TemporaryImportDirectory {
  public:
    TemporaryImportDirectory() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-asset-import-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(project_assets());
        std::filesystem::create_directories(source_directory());
    }

    ~TemporaryImportDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryImportDirectory(const TemporaryImportDirectory&) = delete;
    TemporaryImportDirectory&
    operator=(const TemporaryImportDirectory&) = delete;

    std::filesystem::path project_assets() const {
        return m_path / "project" / "assets";
    }

    std::filesystem::path source_directory() const { return m_path / "source"; }

    std::filesystem::path write_source(
        const std::filesystem::path& path,
        std::string_view content
    ) const {
        const auto result = source_directory() / path;
        std::filesystem::create_directories(result.parent_path());
        std::ofstream stream(result, std::ios::binary);
        stream << content;
        return result;
    }

    std::filesystem::path write_project_asset(
        const std::filesystem::path& path,
        std::string_view content
    ) const {
        const auto result = project_assets() / path;
        std::filesystem::create_directories(result.parent_path());
        std::ofstream stream(result, std::ios::binary);
        stream << content;
        return result;
    }

  private:
    std::filesystem::path m_path;
};

class MockImporter : public AssetImporter {
  public:
    std::string_view name() const override { return "mock"; }
    std::uint32_t version() const override { return 3; }

    std::span<const std::string_view> extensions() const override {
        static constexpr std::string_view extensions[] = {".mock"};
        return extensions;
    }

    AssetImportSettings
    default_settings(const AssetPath& /*destination*/) const override {
        return {{"mode", "test"}};
    }

    Status<std::string> validate(
        const Reader& source,
        const AssetImportContext& /*context*/
    ) const override {
        if (!source.as_string_view().starts_with("valid")) {
            return failure(std::string("invalid mock"));
        }
        return {};
    }

    Result<AssetImportArtifacts, std::string> import(
        const Reader& source,
        const AssetImportContext& context
    ) const override {
        auto status = validate(source, context);
        if (!status) {
            return failure(std::move(status.error()));
        }
        const auto artifact = context.artifact_directory / "mock.bin";
        std::ofstream stream(artifact, std::ios::binary | std::ios::trunc);
        stream.write(
            reinterpret_cast<const char*>(source.data()),
            static_cast<std::streamsize>(source.size())
        );
        if (!stream) {
            return failure(
                "failed to write mock artifact: " + artifact.string()
            );
        }
        return AssetImportArtifacts {
            AssetArtifact {.kind = "mock", .path = "mock.bin"},
        };
    }
};

AssetImporterRegistry mock_registry() {
    AssetImporterRegistry registry;
    registry.emplace<MockImporter>();
    return registry;
}

} // namespace

TEST_CASE("AssetUuid round trips stable text", "[asset][import]") {
    const auto id = AssetUuid::random();
    REQUIRE_FALSE(id.is_nil());
    const auto parsed = AssetUuid::parse(id.as_string());
    REQUIRE(parsed);
    CHECK(*parsed == id);
    CHECK_FALSE(AssetUuid::parse("not-a-uuid"));
}

TEST_CASE(
    "Asset import copies, validates, and registers metadata",
    "[asset][import]"
) {
    TemporaryImportDirectory directory;
    auto registry = mock_registry();
    AssetDatabase database(directory.project_assets());
    const auto source = directory.write_source("face.mock", "valid");

    auto result = import_asset(
        AssetImportRequest {
            .source_file = source,
            .destination = AssetPath("project://textures/face.mock"),
            .settings = {{"custom", "value"}},
        },
        registry,
        database
    );

    REQUIRE(result);
    CHECK(result->copied);
    CHECK(result->metadata.importer == "mock");
    CHECK(result->record.importer_version == 3);
    CHECK(result->metadata.settings.at("mode") == "test");
    CHECK(result->metadata.settings.at("custom") == "value");
    CHECK(result->record.source_hash.size() == 16);
    REQUIRE(result->record.artifacts.size() == 1);
    CHECK(result->record.artifacts.front().kind == "mock");
    CHECK(result->record.artifacts.front().path == "mock.bin");
    CHECK(
        std::filesystem::exists(
            directory.project_assets() / "textures" / "face.mock"
        )
    );
    CHECK(
        std::filesystem::exists(
            directory.project_assets() / "textures" / "face.mock.meta"
        )
    );
    CHECK(
        std::filesystem::exists(
            database.import_record_path(result->metadata.id)
        )
    );
    auto metadata_file = Reader::from_file(
        directory.project_assets() / "textures" / "face.mock.meta"
    );
    REQUIRE(metadata_file);
    CHECK_FALSE(metadata_file->as_string_view().contains("source_hash"));
    CHECK_FALSE(metadata_file->as_string_view().contains("importer_version"));
    auto import_record =
        Reader::from_file(database.import_record_path(result->metadata.id));
    REQUIRE(import_record);
    CHECK(import_record->as_string_view().contains("source_hash"));
    CHECK(import_record->as_string_view().contains("importer_version: 3"));
    CHECK(import_record->as_string_view().contains("kind: mock"));
    CHECK(import_record->as_string_view().contains("path: mock.bin"));
    const auto artifact_path = database.artifact_path(
        AssetPath("project://textures/face.mock"),
        "mock"
    );
    REQUIRE(artifact_path);
    CHECK(std::filesystem::exists(*artifact_path));
    CHECK(
        database.state(AssetPath("project://textures/face.mock")) ==
        AssetImportState::Imported
    );
    REQUIRE(database.path(result->metadata.id));
    CHECK(
        *database.path(result->metadata.id) ==
        AssetPath("project://textures/face.mock")
    );
    auto duplicate = import_asset(
        AssetImportRequest {
            .source_file = source,
            .destination = AssetPath("project://textures/face.mock"),
            .settings = {},
        },
        registry,
        database
    );
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().kind == AssetImportErrorKind::DestinationExists);
    CHECK(
        database.state(AssetPath("project://textures/face.mock")) ==
        AssetImportState::Imported
    );

    AssetDatabase rescanned(directory.project_assets());
    REQUIRE(rescanned.scan());
    const auto* metadata =
        rescanned.metadata(AssetPath("project://textures/face.mock"));
    REQUIRE(metadata);
    CHECK(metadata->id == result->metadata.id);
    REQUIRE(rescanned.import_record(AssetPath("project://textures/face.mock")));
    CHECK(
        rescanned.state(AssetPath("project://textures/face.mock")) ==
        AssetImportState::Imported
    );
}

TEST_CASE("Asset import supports project files in place", "[asset][import]") {
    TemporaryImportDirectory directory;
    auto registry = mock_registry();
    AssetDatabase database(directory.project_assets());
    const auto source = directory.write_project_asset("existing.mock", "valid");

    auto result = import_asset(
        AssetImportRequest {
            .source_file = source,
            .destination = AssetPath("project://existing.mock"),
            .settings = {},
        },
        registry,
        database
    );

    REQUIRE(result);
    CHECK_FALSE(result->copied);
    CHECK(std::filesystem::exists(source.string() + ".meta"));
}

TEST_CASE("Asset import records validation failures", "[asset][import]") {
    TemporaryImportDirectory directory;
    auto registry = mock_registry();
    AssetDatabase database(directory.project_assets());
    const auto source = directory.write_source("broken.mock", "broken");
    const AssetPath destination("project://broken.mock");

    auto result = import_asset(
        AssetImportRequest {
            .source_file = source,
            .destination = destination,
            .settings = {},
        },
        registry,
        database
    );

    REQUIRE_FALSE(result);
    CHECK(result.error().kind == AssetImportErrorKind::Validation);
    CHECK(database.state(destination) == AssetImportState::Failed);
    REQUIRE(database.error(destination));
    CHECK_FALSE(
        std::filesystem::exists(directory.project_assets() / "broken.mock")
    );
}

TEST_CASE(
    "Pending asset import discovers project files once",
    "[asset][import]"
) {
    TemporaryImportDirectory directory;
    auto registry = mock_registry();
    AssetDatabase database(directory.project_assets());
    directory.write_project_asset("valid.mock", "valid");
    directory.write_project_asset("broken.mock", "broken");
    directory.write_project_asset("notes.txt", "ignored");

    auto first = import_pending_assets(registry, database);
    REQUIRE(first);
    REQUIRE(first->imported.size() == 1);
    REQUIRE(first->failed.size() == 1);
    CHECK(first->imported.front().path == AssetPath("project://valid.mock"));
    CHECK(
        first->failed.front().destination == AssetPath("project://broken.mock")
    );
    CHECK(
        std::filesystem::exists(directory.project_assets() / "valid.mock.meta")
    );
    CHECK_FALSE(
        std::filesystem::exists(directory.project_assets() / "notes.txt.meta")
    );

    auto second = import_pending_assets(registry, database);
    REQUIRE(second);
    CHECK(second->imported.empty());
    CHECK(second->failed.empty());

    const auto previous_id = first->imported.front().metadata.id;
    const auto previous_hash = first->imported.front().record.source_hash;
    const auto artifact =
        database.artifact_path(AssetPath("project://valid.mock"), "mock");
    REQUIRE(artifact);
    directory.write_project_asset("valid.mock", "valid changed");
    auto third = import_pending_assets(registry, database);
    REQUIRE(third);
    REQUIRE(third->imported.size() == 1);
    CHECK(third->failed.empty());
    CHECK_FALSE(third->imported.front().copied);
    CHECK(third->imported.front().metadata.id == previous_id);
    CHECK(third->imported.front().record.source_hash != previous_hash);
    auto changed_artifact = Reader::from_file(*artifact);
    REQUIRE(changed_artifact);
    CHECK(changed_artifact->as_string_view() == "valid changed");

    const auto valid_asset =
        directory.write_project_asset("valid.mock", "broken after import");
    auto fourth = import_pending_assets(registry, database);
    REQUIRE(fourth);
    REQUIRE(fourth->failed.size() == 1);
    CHECK(
        database.state(AssetPath("project://valid.mock")) ==
        AssetImportState::Failed
    );
    auto preserved_artifact = Reader::from_file(*artifact);
    REQUIRE(preserved_artifact);
    CHECK(preserved_artifact->as_string_view() == "valid changed");
    auto fifth = import_pending_assets(registry, database);
    REQUIRE(fifth);
    CHECK(fifth->failed.empty());

    directory.write_project_asset("valid.mock", "valid repaired");
    auto retry = import_asset(
        AssetImportRequest {
            .source_file = valid_asset,
            .destination = AssetPath("project://valid.mock"),
            .settings = {},
        },
        registry,
        database
    );
    REQUIRE(retry);
    CHECK(retry->metadata.id == previous_id);
    CHECK(
        database.state(AssetPath("project://valid.mock")) ==
        AssetImportState::Imported
    );
}

TEST_CASE("Legacy metadata migrates into the import cache", "[asset][import]") {
    TemporaryImportDirectory directory;
    auto registry = mock_registry();
    const auto id = AssetUuid::random();
    directory.write_project_asset("legacy.mock", "valid");
    const auto metadata_file = directory.write_project_asset(
        "legacy.mock.meta",
        "id: " + id.as_string() +
            "\nimporter: mock\nversion: 3\nsource_hash: old\nsettings:\n  "
            "mode: test\n"
    );

    AssetDatabase database(directory.project_assets());
    REQUIRE(database.scan());
    CHECK(
        database.state(AssetPath("project://legacy.mock")) ==
        AssetImportState::Unimported
    );

    auto report = import_pending_assets(registry, database);
    REQUIRE(report);
    REQUIRE(report->imported.size() == 1);
    CHECK(report->imported.front().metadata.id == id);
    CHECK(std::filesystem::exists(database.import_record_path(id)));
    auto migrated_metadata = Reader::from_file(metadata_file);
    REQUIRE(migrated_metadata);
    CHECK_FALSE(migrated_metadata->as_string_view().contains("source_hash"));
    CHECK_FALSE(migrated_metadata->as_string_view().contains("version:"));
}
