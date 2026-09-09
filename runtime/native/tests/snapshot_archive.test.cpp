#include "runtime_host/snapshot_archive.hpp"

#include "project/project.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace ets;

namespace {

class MetadataProject {
  private:
    std::filesystem::path m_root;

  public:
    MetadataProject() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("entisium-snapshot-metadata-" + std::to_string(suffix));
        std::filesystem::create_directories(m_root / "assets");
        std::ofstream(m_root / "project.yaml")
            << "name: Metadata Test\n"
               "asset_directory: assets\n"
               "plugin: project://main.luau#MainPlugin\n";
        write_script("export local value = 1\n");
    }

    ~MetadataProject() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    void write_script(std::string_view source) const {
        std::ofstream stream(
            m_root / "assets" / "main.luau",
            std::ios::binary | std::ios::trunc
        );
        stream << source;
    }

    const std::filesystem::path project_file() const {
        return m_root / "project.yaml";
    }
};

} // namespace

TEST_CASE(
    "Runtime host derives stable snapshot archive metadata from a project",
    "[runtime-host][snapshot][archive][metadata]"
) {
    MetadataProject fixture;
    auto project = Project::load(fixture.project_file());
    REQUIRE(project);

    auto first =
        runtime_host::make_snapshot_archive_metadata(*project, "test-build");
    auto second =
        runtime_host::make_snapshot_archive_metadata(*project, "test-build");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == *second);
    CHECK(first->project == "Metadata Test");
    CHECK(first->engine_build == "test-build");
    CHECK_FALSE(first->runtime_signature.empty());
    CHECK_FALSE(first->script_hash.empty());

    fixture.write_script("export local value = 2\n");
    auto changed =
        runtime_host::make_snapshot_archive_metadata(*project, "test-build");
    REQUIRE(changed);
    CHECK(changed->runtime_signature == first->runtime_signature);
    CHECK(changed->script_hash != first->script_hash);
}

TEST_CASE(
    "Runtime host derives a stable build id from the current executable",
    "[runtime-host][snapshot][archive][build]"
) {
    auto first = runtime_host::current_runtime_build_id();
    auto second = runtime_host::current_runtime_build_id();
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == *second);
    CHECK(first->starts_with("entisium-runtime-host:"));
}
