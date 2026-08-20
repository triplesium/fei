#include "runtime_inspection_snapshot/checkpoint.hpp"

#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "runtime_inspection/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

using namespace fei;
using namespace fei::runtime_inspection;
using namespace fei::runtime_inspection::checkpoint;

namespace {

struct Position {
    int x {};
};

void register_types() {
    static bool registered = false;
    if (registered) {
        return;
    }
    Registry::instance()
        .register_cls<Position>({"checkpoint_inspection_test"}, "Position")
        .add_property("x", &Position::x);
    registered = true;
}

InspectionInvocation invocation(
    std::string_view provider,
    std::string_view schema,
    std::string_view payload
) {
    return InspectionInvocation {
        .provider = provider,
        .schema = schema,
        .payload_json = payload,
    };
}

Entity find_position_entity(const World& world) {
    for (const auto& [_, archetype] : world.archetypes()) {
        if (archetype.has_component(type_id<Position>()) &&
            !archetype.entities().empty()) {
            return archetype.entities().front();
        }
    }
    return {};
}

snapshot::SnapshotArchiveMetadata archive_metadata() {
    return {
        .project = "checkpoint-inspection-test",
        .engine_build = "test-build-1",
        .runtime_signature = "test-runtime",
        .script_hash = "no-scripts",
    };
}

void install_archive_resources(World& world) {
    world.add_resource(snapshot::CheckpointStore {});
    world.add_resource(archive_metadata());
    auto& snapshot_registry =
        world.resource<snapshot::CheckpointStore>().registry();
    snapshot_registry.resource<snapshot::CheckpointStore>(
        snapshot::ResourcePolicy::Ignore
    );
    snapshot_registry.resource<snapshot::SnapshotArchiveMetadata>(
        snapshot::ResourcePolicy::Ignore
    );
}

class CheckpointTempDirectory {
  private:
    std::filesystem::path m_path;

  public:
    CheckpointTempDirectory() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-checkpoint-inspection-" + std::to_string(suffix));
        std::filesystem::create_directories(m_path);
    }

    ~CheckpointTempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    std::filesystem::path file(std::string_view name) const {
        return m_path / name;
    }
};

} // namespace

TEST_CASE(
    "Checkpoint inspection providers expose create, list, and restore",
    "[runtime-inspection][snapshot][checkpoint]"
) {
    register_types();
    World world;
    world.add_resource(snapshot::CheckpointStore {});
    const auto original = world.entity();
    world.add_component(original, Position {.x = 7});

    InspectionRegistry registry;
    REQUIRE(register_checkpoint_inspection_providers(registry));
    registry.freeze();

    auto created = registry.dispatch(
        world,
        invocation(
            CreateCheckpointProvider::id,
            CreateCheckpointProvider::schema,
            R"({"name":"turn-0"})"
        )
    );
    REQUIRE(created);
    const auto created_json = nlohmann::json::parse(*created);
    CHECK(created_json.at("name") == "turn-0");
    CHECK(created_json.at("entity_count") == 1);
    CHECK(created_json.at("byte_size").get<std::size_t>() > 0);

    world.get_component_rw<Position>(original)->x = 99;
    const auto discarded = world.entity();
    world.add_component(discarded, Position {.x = 100});

    auto listed = registry.dispatch(
        world,
        invocation(
            ListCheckpointsProvider::id,
            ListCheckpointsProvider::schema,
            "{}"
        )
    );
    REQUIRE(listed);
    const auto listed_json = nlohmann::json::parse(*listed);
    REQUIRE(listed_json.at("checkpoints").size() == 1);
    CHECK(listed_json.at("checkpoints").at(0).at("name") == "turn-0");

    auto audited = registry.dispatch(
        world,
        invocation(
            AuditCheckpointProvider::id,
            AuditCheckpointProvider::schema,
            "{}"
        )
    );
    REQUIRE(audited);
    const auto audited_json = nlohmann::json::parse(*audited);
    CHECK(audited_json.at("ready") == true);
    CHECK(audited_json.at("complete") == false);
    CHECK(audited_json.at("entity_count") == 2);
    REQUIRE_FALSE(audited_json.at("components").empty());
    CHECK(audited_json.at("components").at(0).at("disposition") == "snapshot");

    auto restored = registry.dispatch(
        world,
        invocation(
            RestoreCheckpointProvider::id,
            RestoreCheckpointProvider::schema,
            R"({"name":"turn-0"})"
        )
    );
    REQUIRE(restored);
    CHECK_FALSE(world.has_entity(original));
    CHECK_FALSE(world.has_entity(discarded));
    const auto restored_entity = find_position_entity(world);
    CHECK(world.has_entity(restored_entity));
    CHECK(world.get_component<Position>(restored_entity).x == 7);
    CHECK(nlohmann::json::parse(*restored).at("restored_entity_count") == 1);
}

TEST_CASE(
    "Checkpoint inspection providers support strict delete and clear",
    "[runtime-inspection][snapshot][checkpoint][lifecycle]"
) {
    register_types();
    World world;
    world.add_resource(snapshot::CheckpointStore {});
    const auto entity = world.entity();
    world.add_component(entity, Position {.x = 4});
    auto& store = world.resource<snapshot::CheckpointStore>();
    store.registry().resource<snapshot::CheckpointStore>(
        snapshot::ResourcePolicy::Ignore
    );

    InspectionRegistry registry;
    REQUIRE(register_checkpoint_inspection_providers(registry));
    registry.freeze();

    auto strict = registry.dispatch(
        world,
        invocation(
            CreateCheckpointProvider::id,
            CreateCheckpointProvider::schema,
            R"({"name":"strict","strict":true})"
        )
    );
    REQUIRE(strict);

    auto removed = registry.dispatch(
        world,
        invocation(
            DeleteCheckpointProvider::id,
            DeleteCheckpointProvider::schema,
            R"({"name":"strict"})"
        )
    );
    REQUIRE(removed);
    CHECK(nlohmann::json::parse(*removed).at("name") == "strict");

    REQUIRE(registry.dispatch(
        world,
        invocation(
            CreateCheckpointProvider::id,
            CreateCheckpointProvider::schema,
            R"({"name":"one"})"
        )
    ));
    REQUIRE(registry.dispatch(
        world,
        invocation(
            CreateCheckpointProvider::id,
            CreateCheckpointProvider::schema,
            R"({"name":"two"})"
        )
    ));
    auto cleared = registry.dispatch(
        world,
        invocation(
            ClearCheckpointsProvider::id,
            ClearCheckpointsProvider::schema,
            "{}"
        )
    );
    REQUIRE(cleared);
    CHECK(nlohmann::json::parse(*cleared).at("removed") == 2);
}

TEST_CASE(
    "Checkpoint inspection providers validate requests and missing names",
    "[runtime-inspection][snapshot][checkpoint][validation]"
) {
    World world;
    world.add_resource(snapshot::CheckpointStore {});
    InspectionRegistry registry;
    REQUIRE(register_checkpoint_inspection_providers(registry));

    const auto invalid = registry.dispatch(
        world,
        invocation(
            CreateCheckpointProvider::id,
            CreateCheckpointProvider::schema,
            R"({"name":"bad name"})"
        )
    );
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == InspectionErrorKind::InvalidRequest);

    const auto missing = registry.dispatch(
        world,
        invocation(
            RestoreCheckpointProvider::id,
            RestoreCheckpointProvider::schema,
            R"({"name":"missing"})"
        )
    );
    REQUIRE_FALSE(missing);
    CHECK(missing.error().kind == InspectionErrorKind::NotFound);

    const auto non_empty_list = registry.dispatch(
        world,
        invocation(
            ListCheckpointsProvider::id,
            ListCheckpointsProvider::schema,
            R"({"extra":true})"
        )
    );
    REQUIRE_FALSE(non_empty_list);
    CHECK(non_empty_list.error().kind == InspectionErrorKind::InvalidRequest);
}

TEST_CASE(
    "Checkpoint inspection providers export and import disk archives",
    "[runtime-inspection][snapshot][checkpoint][archive]"
) {
    register_types();
    CheckpointTempDirectory temporary;
    const auto path = temporary.file("turn-0.fei-snapshot.json");

    World source;
    install_archive_resources(source);
    const auto original = source.entity();
    source.add_component(original, Position {.x = 23});

    InspectionRegistry registry;
    REQUIRE(register_checkpoint_inspection_providers(registry));
    registry.freeze();
    REQUIRE(registry.dispatch(
        source,
        invocation(
            CreateCheckpointProvider::id,
            CreateCheckpointProvider::schema,
            R"({"name":"turn-0","strict":true})"
        )
    ));

    const auto file_request =
        nlohmann::json {{"name", "turn-0"}, {"path", path.string()}}.dump();
    auto exported = registry.dispatch(
        source,
        invocation(
            ExportCheckpointProvider::id,
            ExportCheckpointProvider::schema,
            file_request
        )
    );
    REQUIRE(exported);
    const auto exported_json = nlohmann::json::parse(*exported);
    CHECK(exported_json.at("name") == "turn-0");
    CHECK(exported_json.at("entity_count") == 1);
    CHECK(exported_json.at("file_size").get<std::size_t>() > 0);
    CHECK(std::filesystem::exists(path));

    World changed_topology;
    install_archive_resources(changed_topology);
    changed_topology.add_systems(0x7a10, [] {
    });
    const auto topology_mismatch = registry.dispatch(
        changed_topology,
        invocation(
            ImportCheckpointProvider::id,
            ImportCheckpointProvider::schema,
            file_request
        )
    );
    REQUIRE_FALSE(topology_mismatch);
    CHECK(topology_mismatch.error().kind == InspectionErrorKind::Conflict);
    CHECK(
        topology_mismatch.error().message.find("runtime_signature") !=
        std::string::npos
    );

    World target;
    install_archive_resources(target);
    const auto import_request =
        nlohmann::json {{"name", "from-disk"}, {"path", path.string()}}.dump();
    auto imported = registry.dispatch(
        target,
        invocation(
            ImportCheckpointProvider::id,
            ImportCheckpointProvider::schema,
            import_request
        )
    );
    REQUIRE(imported);
    CHECK(nlohmann::json::parse(*imported).at("name") == "from-disk");

    REQUIRE(registry.dispatch(
        target,
        invocation(
            RestoreCheckpointProvider::id,
            RestoreCheckpointProvider::schema,
            R"({"name":"from-disk"})"
        )
    ));
    const auto restored = find_position_entity(target);
    REQUIRE(target.has_entity(restored));
    CHECK(target.get_component<Position>(restored).x == 23);

    target.resource<snapshot::SnapshotArchiveMetadata>().engine_build =
        "different-build";
    const auto incompatible = registry.dispatch(
        target,
        invocation(
            ImportCheckpointProvider::id,
            ImportCheckpointProvider::schema,
            import_request
        )
    );
    REQUIRE_FALSE(incompatible);
    CHECK(incompatible.error().kind == InspectionErrorKind::Conflict);
    CHECK(
        incompatible.error().message.find("engine_build") != std::string::npos
    );

    target.resource<snapshot::SnapshotArchiveMetadata>().engine_build =
        archive_metadata().engine_build;
    target.resource<snapshot::SnapshotArchiveMetadata>().script_hash =
        "changed-scripts";
    const auto changed_scripts = registry.dispatch(
        target,
        invocation(
            ImportCheckpointProvider::id,
            ImportCheckpointProvider::schema,
            import_request
        )
    );
    REQUIRE_FALSE(changed_scripts);
    CHECK(changed_scripts.error().kind == InspectionErrorKind::Conflict);
    CHECK(
        changed_scripts.error().message.find("script_hash") != std::string::npos
    );
}
