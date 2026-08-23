#include "runtime_inspection_ecs/query.hpp"

#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "runtime_inspection_ecs/entity.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

using namespace ets;
using namespace ets::runtime_inspection;
using namespace ets::runtime_inspection::ecs;

namespace runtime_query_test {

struct Position {
    float x {0};
    float y {0};
};

struct Velocity {
    float x {0};
    float y {0};
};

struct Hidden {};

struct Opaque {
    int value {0};
};

void register_query_test_types() {
    static bool registered = false;
    if (!registered) {
        register_generated_reflection();
        auto& registry = Registry::instance();
        registry.register_cls<Position>()
            .add_property("x", &Position::x)
            .add_property("y", &Position::y);
        registry.register_cls<Velocity>()
            .add_property("x", &Velocity::x)
            .add_property("y", &Velocity::y);
        registry.register_cls<Hidden>();
        registry.register_type<Opaque>();
        registered = true;
    }
}

template<class T>
std::string reflected_name() {
    return std::string(type_name<T>());
}

} // namespace runtime_query_test

TEST_CASE(
    "ECS query inspection returns deterministic bounded component snapshots",
    "[runtime-inspection][ecs][query]"
) {
    using namespace runtime_query_test;
    register_query_test_types();

    World world;
    const auto first = world.entity();
    world.add_component(first, Position {.x = 1, .y = 2});
    world.add_component(first, Velocity {.x = 3, .y = 4});

    const auto missing_velocity = world.entity();
    world.add_component(missing_velocity, Position {.x = 5, .y = 6});

    const auto hidden = world.entity();
    world.add_component(hidden, Position {.x = 7, .y = 8});
    world.add_component(hidden, Velocity {.x = 9, .y = 10});
    world.add_component(hidden, Hidden {});

    const auto last = world.entity();
    world.add_component(last, Position {.x = 11, .y = 12});
    world.add_component(last, Velocity {.x = 13, .y = 14});

    const QueryInspectionProvider provider;
    auto snapshot = provider.inspect(
        world,
        QueryRequest {
            .components = {reflected_name<Position>()},
            .with = {reflected_name<Velocity>()},
            .without = {reflected_name<Hidden>()},
            .limit = 1,
        }
    );
    REQUIRE(snapshot);
    REQUIRE(snapshot->matched == 2);
    REQUIRE(snapshot->rows.size() == 1);
    REQUIRE(snapshot->rows.front().entity == first);

    auto response = encode_query_snapshot_json(*snapshot);
    REQUIRE(response);
    const auto json = nlohmann::json::parse(*response);
    REQUIRE(json.at("truncated") == true);
    REQUIRE(json.at("columns").size() == 1);
    REQUIRE(json.at("columns").at(0).at("name") == reflected_name<Position>());
    const auto& position =
        json.at("rows").at(0).at("components").at(reflected_name<Position>());
    REQUIRE(position.at("x") == 1);
    REQUIRE(position.at("y") == 2);
    REQUIRE(first < last);
}

TEST_CASE(
    "ECS query inspection includes every matching world entity",
    "[runtime-inspection][ecs][query]"
) {
    World world;
    const auto first = world.entity();
    const auto second = world.entity();

    const QueryInspectionProvider provider;
    auto snapshot = provider.inspect(world, QueryRequest {.limit = 10});
    REQUIRE(snapshot);
    REQUIRE(snapshot->matched == 2);
    REQUIRE(snapshot->rows.at(0).entity == first);
    REQUIRE(snapshot->rows.at(1).entity == second);
}

TEST_CASE(
    "ECS query inspection validates selectors and serialization",
    "[runtime-inspection][ecs][query]"
) {
    using namespace runtime_query_test;
    register_query_test_types();

    World world;
    const auto entity = world.entity();
    world.add_component(entity, Opaque {.value = 7});

    const QueryInspectionProvider provider;
    auto bad_limit = provider.inspect(world, QueryRequest {.limit = 0});
    REQUIRE_FALSE(bad_limit);
    REQUIRE(bad_limit.error().kind == InspectionErrorKind::InvalidRequest);

    auto missing = provider.inspect(
        world,
        QueryRequest {
            .components = {"MissingComponent"},
            .limit = 10,
        }
    );
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().kind == InspectionErrorKind::NotFound);

    auto conflict = provider.inspect(
        world,
        QueryRequest {
            .components = {reflected_name<Position>()},
            .without = {reflected_name<Position>()},
            .limit = 10,
        }
    );
    REQUIRE_FALSE(conflict);
    REQUIRE(conflict.error().kind == InspectionErrorKind::InvalidRequest);

    auto unsupported = provider.inspect(
        world,
        QueryRequest {
            .components = {reflected_name<Opaque>()},
            .limit = 10,
        }
    );
    REQUIRE_FALSE(unsupported);
    REQUIRE(unsupported.error().kind == InspectionErrorKind::Unsupported);
    REQUIRE(
        unsupported.error().message.find("Failed to serialize component") !=
        std::string::npos
    );
}

TEST_CASE(
    "ECS query inspection registers a JSON provider",
    "[runtime-inspection][ecs][registry]"
) {
    InspectionRegistry registry;
    REQUIRE(register_query_inspection_provider(registry));
    registry.freeze();

    World world;
    const auto entity = world.entity();
    auto response = registry.dispatch(
        world,
        InspectionInvocation {
            .provider = QueryInspectionProvider::id,
            .schema = QueryInspectionProvider::schema,
            .payload_json =
                R"({"components":[],"with":[],"without":[],"limit":10})",
        }
    );
    REQUIRE(response);
    const auto json = nlohmann::json::parse(*response);
    REQUIRE(json.at("matched") == 1);
    REQUIRE(json.at("rows").at(0).at("entity") == entity.value);
}

TEST_CASE(
    "ECS inspection providers publish machine-readable contracts",
    "[runtime-inspection][ecs][contract]"
) {
    InspectionRegistry registry;
    REQUIRE(register_entity_inspection_provider(registry));
    REQUIRE(register_query_inspection_provider(registry));
    REQUIRE(registry.descriptors().size() == 2);

    const auto& entity = registry.descriptors()[0];
    CHECK(entity.read_only);
    CHECK(entity.cost == InspectionCost::Low);
    auto entity_request = nlohmann::json::parse(entity.request_schema_json);
    CHECK(entity_request.at("required") == nlohmann::json::array({"entity"}));
    CHECK(entity_request.at("additionalProperties") == false);

    const auto& query = registry.descriptors()[1];
    CHECK(query.read_only);
    CHECK(query.cost == InspectionCost::Moderate);
    auto query_request = nlohmann::json::parse(query.request_schema_json);
    CHECK(query_request.at("properties").at("limit").at("default") == 50);
    CHECK(query_request.at("properties").at("limit").at("maximum") == 200);
    CHECK(query_request.at("properties").at("components").at("maxItems") == 32);

    auto query_response = nlohmann::json::parse(query.response_schema_json);
    CHECK(query_response.at("properties").at("rows").at("maxItems") == 200);
}
