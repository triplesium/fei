#include "runtime_inspection_ecs/entity.hpp"

#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

using namespace ets;
using namespace ets::runtime_inspection;
using namespace ets::runtime_inspection::ecs;

namespace {

struct Position {
    float x {0};
    float y {0};
};

struct Opaque {
    int value {0};
};

void register_test_types() {
    static bool registered = false;
    if (registered) {
        return;
    }
    register_generated_reflection();
    Registry::instance()
        .register_cls<Position>()
        .add_property("x", &Position::x)
        .add_property("y", &Position::y);
    Registry::instance().register_type<Opaque>();
    registered = true;
}

} // namespace

TEST_CASE(
    "ECS entity inspection returns a transport-neutral snapshot",
    "[runtime-inspection][ecs][entity]"
) {
    register_test_types();
    World world;
    const auto entity = world.entity();
    world.add_component(entity, Position {.x = 2, .y = 4});
    world.add_component(entity, Opaque {.value = 7});

    const auto expected_tick = world.read_change_tick();
    const EntityInspectionProvider provider;
    auto snapshot =
        provider.inspect(world, EntityInspectRequest {.entity = entity});

    REQUIRE(snapshot);
    CHECK(snapshot->observed_tick == expected_tick);
    CHECK(snapshot->entity == entity);
    CHECK(snapshot->archetype_id > 0);
    REQUIRE(snapshot->components.size() == 2);

    const auto position = std::ranges::find_if(
        snapshot->components,
        [](const ComponentSnapshot& component) {
            return component.id == type_id<Position>();
        }
    );
    REQUIRE(position != snapshot->components.end());
    CHECK(position->serialized);
    CHECK_FALSE(position->error);

    const auto opaque = std::ranges::find_if(
        snapshot->components,
        [](const ComponentSnapshot& component) {
            return component.id == type_id<Opaque>();
        }
    );
    REQUIRE(opaque != snapshot->components.end());
    CHECK_FALSE(opaque->serialized);
    CHECK(opaque->error);

    auto json = encode_entity_snapshot_json(*snapshot);
    REQUIRE(json);
    const auto document = nlohmann::json::parse(*json);
    CHECK(document.at("entity") == entity.value);
    CHECK(document.at("component_count") == 2);
}

TEST_CASE(
    "ECS entity inspection reports missing entities without transport codes",
    "[runtime-inspection][ecs][entity]"
) {
    World world;
    const auto entity = world.entity();
    world.despawn(entity);

    const EntityInspectionProvider provider;
    auto snapshot =
        provider.inspect(world, EntityInspectRequest {.entity = entity});

    REQUIRE_FALSE(snapshot);
    CHECK(snapshot.error().kind == InspectionErrorKind::NotFound);
}

TEST_CASE(
    "ECS entity inspection accepts a transport-neutral JSON request",
    "[runtime-inspection][ecs][entity]"
) {
    register_test_types();
    World world;
    const auto entity = world.entity();
    world.add_component(entity, Position {.x = 5, .y = 8});

    auto response = inspect_entity_json(world, R"({"entity":0})");
    REQUIRE(response);
    const auto json = nlohmann::json::parse(*response);
    CHECK(json.at("entity") == entity.value);
    CHECK(json.at("component_count") == 1);

    auto invalid = inspect_entity_json(world, R"({"entity":"zero"})");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == InspectionErrorKind::InvalidRequest);
}

TEST_CASE(
    "ECS entity inspection registers with the provider registry",
    "[runtime-inspection][ecs][registry]"
) {
    register_test_types();
    InspectionRegistry registry;
    REQUIRE(register_entity_inspection_provider(registry));
    registry.freeze();

    World world;
    const auto entity = world.entity();
    auto response = registry.dispatch(
        world,
        InspectionInvocation {
            .provider = EntityInspectionProvider::id,
            .schema = EntityInspectionProvider::schema,
            .payload_json = R"({"entity":0})",
        }
    );
    REQUIRE(response);
    const auto json = nlohmann::json::parse(*response);
    CHECK(json.at("entity") == entity.value);
}
