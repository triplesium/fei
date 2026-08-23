#include "runtime_inspection_ecs/world_summary.hpp"

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

namespace world_summary_test {

struct Position {
    float x {0};
};

struct Velocity {
    float x {0};
};

struct Retired {};

void register_summary_test_types() {
    static bool registered = false;
    if (registered) {
        return;
    }
    register_generated_reflection();
    Registry::instance().register_cls<Position>().add_property(
        "x",
        &Position::x
    );
    Registry::instance().register_cls<Velocity>().add_property(
        "x",
        &Velocity::x
    );
    Registry::instance().register_cls<Retired>();
    registered = true;
}

const WorldComponentTypeSummary&
find_component(const WorldSummary& summary, TypeId id) {
    const auto component = std::ranges::find_if(
        summary.component_types,
        [id](const WorldComponentTypeSummary& candidate) {
            return candidate.id == id;
        }
    );
    REQUIRE(component != summary.component_types.end());
    return *component;
}

} // namespace world_summary_test

TEST_CASE(
    "ECS world summary reports global counts before truncating details",
    "[runtime-inspection][ecs][world-summary]"
) {
    using namespace world_summary_test;
    register_summary_test_types();

    World world;
    world.entity();

    const auto positioned = world.entity();
    world.add_component(positioned, Position {.x = 1});

    const auto moving = world.entity();
    world.add_component(moving, Position {.x = 2});
    world.add_component(moving, Velocity {.x = 3});

    const auto retired = world.entity();
    world.add_component(retired, Retired {});
    world.despawn(retired);

    const WorldSummaryInspectionProvider provider;
    auto summary = provider.inspect(
        world,
        WorldSummaryRequest {
            .archetype_limit = 2,
            .include_empty_archetypes = false,
        }
    );
    REQUIRE(summary);
    CHECK(summary->entity_count == 3);
    CHECK(summary->known_archetype_count >= 4);
    CHECK(summary->matched_archetype_count == 3);
    REQUIRE(summary->archetypes.size() == 2);
    CHECK(summary->archetypes.at(0).id < summary->archetypes.at(1).id);
    REQUIRE(summary->component_types.size() == 2);

    const auto& position = find_component(*summary, type_id<Position>());
    REQUIRE(position.name);
    CHECK(*position.name == type_name<Position>());
    CHECK(position.entity_count == 2);
    CHECK(position.archetype_count == 2);

    const auto& velocity = find_component(*summary, type_id<Velocity>());
    CHECK(velocity.entity_count == 1);
    CHECK(velocity.archetype_count == 1);
}

TEST_CASE(
    "ECS world summary can include known empty archetypes",
    "[runtime-inspection][ecs][world-summary]"
) {
    using namespace world_summary_test;
    register_summary_test_types();

    World world;
    const auto entity = world.entity();
    world.add_component(entity, Position {.x = 1});

    const WorldSummaryInspectionProvider provider;
    auto active = provider.inspect(
        world,
        WorldSummaryRequest {
            .archetype_limit = 512,
            .include_empty_archetypes = false,
        }
    );
    REQUIRE(active);
    CHECK(active->known_archetype_count == 2);
    CHECK(active->matched_archetype_count == 1);

    auto all = provider.inspect(
        world,
        WorldSummaryRequest {
            .archetype_limit = 512,
            .include_empty_archetypes = true,
        }
    );
    REQUIRE(all);
    CHECK(all->matched_archetype_count == 2);
    CHECK(
        std::ranges::any_of(
            all->archetypes,
            [](const WorldArchetypeSummary& archetype) {
                return archetype.entity_count == 0;
            }
        )
    );
}

TEST_CASE(
    "ECS world summary validates limits and dispatches JSON requests",
    "[runtime-inspection][ecs][world-summary][registry]"
) {
    world_summary_test::register_summary_test_types();
    World world;
    world.entity();
    const WorldSummaryInspectionProvider provider;
    auto invalid = provider.inspect(
        world,
        WorldSummaryRequest {
            .archetype_limit = 0,
            .include_empty_archetypes = false,
        }
    );
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == InspectionErrorKind::InvalidRequest);

    InspectionRegistry registry;
    REQUIRE(register_world_summary_inspection_provider(registry));
    registry.freeze();
    auto response = registry.dispatch(
        world,
        InspectionInvocation {
            .provider = WorldSummaryInspectionProvider::id,
            .schema = WorldSummaryInspectionProvider::schema,
            .payload_json =
                R"({"archetype_limit":128,"include_empty_archetypes":false})",
        }
    );
    if (!response) {
        FAIL("World summary dispatch failed: " << response.error().message);
    }
    const auto json = nlohmann::json::parse(*response);
    CHECK(json.at("entity_count") == 1);
    CHECK(json.at("returned_archetype_count") == 1);
    CHECK(json.at("truncated") == false);
}
