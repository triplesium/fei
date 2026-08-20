#include "runtime_inspection_snapshot/checkpoint.hpp"

#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "runtime_inspection/registry.hpp"

#include <catch2/catch_test_macros.hpp>
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
