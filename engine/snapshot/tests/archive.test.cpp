#include "snapshot/archive.hpp"

#include "ecs/removed_components.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace fei;

namespace {

struct ArchivePosition {
    int x {};
    Entity target;
};

struct ArchiveMarker {};

struct ArchiveState {
    int score {};
};

constexpr ScheduleId ArchiveSchedule = 0x5a20;

void register_archive_types() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    Registry::instance()
        .register_cls<ArchivePosition>({"snapshot_archive_test"}, "Position")
        .add_property("x", &ArchivePosition::x)
        .add_property("target", &ArchivePosition::target);
    Registry::instance().register_cls<ArchiveMarker>(
        {"snapshot_archive_test"},
        "Marker"
    );
    Registry::instance()
        .register_cls<ArchiveState>({"snapshot_archive_test"}, "State")
        .add_property("score", &ArchiveState::score);
}

snapshot::SnapshotArchiveMetadata archive_metadata() {
    return {
        .project = "snapshot-archive-test",
        .engine_build = "test-build-1",
        .runtime_signature = "empty-schedule-topology-v1",
        .script_hash = "no-scripts",
    };
}

class ArchiveTempDirectory {
  private:
    std::filesystem::path m_path;

  public:
    ArchiveTempDirectory() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-snapshot-archive-" + std::to_string(suffix));
        std::filesystem::create_directories(m_path);
    }

    ~ArchiveTempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    std::filesystem::path file(std::string_view name) const {
        return m_path / name;
    }
};

snapshot::CheckpointStore make_checkpoint_store(World& world) {
    snapshot::CheckpointStore checkpoints;
    checkpoints.registry().resource<ArchiveState>(
        snapshot::ResourcePolicy::Snapshot
    );

    const auto entity = world.entity();
    world.add_component(entity, ArchivePosition {.x = 17, .target = entity});
    world.add_component(entity, ArchiveMarker {});
    world.remove_component<ArchiveMarker>(entity);
    world.add_resource(ArchiveState {.score = 42});

    REQUIRE(checkpoints.create("turn-0", world, true));
    return checkpoints;
}

} // namespace

TEST_CASE(
    "Snapshot archive persists a checkpoint across World instances",
    "[snapshot][archive][file][restore]"
) {
    register_archive_types();
    ArchiveTempDirectory temporary;
    const auto path = temporary.file("turn-0.fei-snapshot.json");

    World source;
    auto source_checkpoints = make_checkpoint_store(source);
    auto exported =
        source_checkpoints.export_file("turn-0", path, archive_metadata());
    REQUIRE(exported);
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) > 0);

    World restored_world;
    snapshot::CheckpointStore restored_checkpoints;
    auto imported = restored_checkpoints
                        .import_file("disk-turn-0", path, archive_metadata());
    REQUIRE(imported);
    CHECK(imported->entity_count == 1);
    CHECK(imported->resource_count == 1);

    auto restored = restored_checkpoints.restore("disk-turn-0", restored_world);
    const auto restore_info =
        restored.has_value() ?
            std::string {} :
            restored.error().path + ": " + restored.error().message;
    INFO(restore_info);
    REQUIRE(restored);
    const auto entity = restored->entity(1);
    REQUIRE(restored_world.has_entity(entity));
    const auto& position =
        restored_world.get_component<ArchivePosition>(entity);
    CHECK(position.x == 17);
    CHECK(position.target == entity);
    CHECK(restored_world.resource<ArchiveState>().score == 42);

    std::size_t removed_count = 0;
    restored_world.run_system_once(
        [&removed_count, entity](RemovedComponents<ArchiveMarker> removed) {
            while (const auto removed_entity = removed.next()) {
                CHECK(*removed_entity == entity);
                ++removed_count;
            }
        }
    );
    CHECK(removed_count == 1);
}

TEST_CASE(
    "Snapshot archive rejects corrupt and incompatible files",
    "[snapshot][archive][validation]"
) {
    register_archive_types();
    ArchiveTempDirectory temporary;

    SECTION("compatibility metadata must match") {
        const auto path = temporary.file("compatible.fei-snapshot.json");
        World source;
        auto checkpoints = make_checkpoint_store(source);
        REQUIRE(checkpoints.export_file("turn-0", path, archive_metadata()));

        auto incompatible = archive_metadata();
        incompatible.engine_build = "different-build";
        auto loaded = snapshot::load_archive_file(path, incompatible);
        REQUIRE_FALSE(loaded);
        CHECK(
            loaded.error().kind ==
            snapshot::SnapshotError::Kind::IncompatibleArchive
        );
        CHECK(loaded.error().path == "compatibility.engine_build");
    }

    SECTION("malformed JSON is rejected") {
        const auto path = temporary.file("corrupt.fei-snapshot.json");
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << "{not-json";
        }
        auto loaded = snapshot::load_archive_file(path, archive_metadata());
        REQUIRE_FALSE(loaded);
        CHECK(
            loaded.error().kind ==
            snapshot::SnapshotError::Kind::ArchiveFormatFailed
        );
    }
}

TEST_CASE(
    "Snapshot archive rejects stateful C++ callable state",
    "[snapshot][archive][system]"
) {
    register_archive_types();
    ArchiveTempDirectory temporary;
    const auto path = temporary.file("stateful.fei-snapshot.json");

    World world;
    world.add_systems(
        ArchiveSchedule,
        checkpointed_system([counter = 0]() mutable {
            ++counter;
        })
    );
    snapshot::CheckpointStore checkpoints;
    REQUIRE(checkpoints.create("stateful", world));

    auto exported =
        checkpoints.export_file("stateful", path, archive_metadata());
    REQUIRE_FALSE(exported);
    CHECK(
        exported.error().kind ==
        snapshot::SnapshotError::Kind::PersistentStateUnsupported
    );
    CHECK(exported.error().path.find("executor") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(path));
}
