#include "query.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "devtools/bridge.hpp"
#include "devtools/json.hpp"
#include "devtools/types.hpp"
#include "devtools_ecs/plugin.hpp"
#include "ecs/query.hpp"
#include "ecs/world.hpp"
#include "entity_inspect.hpp"
#include "refl/cls.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

using namespace fei;
using namespace fei::devtools;
using namespace fei::devtools::ecs;

namespace ecs_query_test {

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

} // namespace ecs_query_test

TEST_CASE(
    "ECS queries return deterministic bounded component snapshots",
    "[devtools][ecs][query]"
) {
    using namespace ecs_query_test;
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

    auto response = execute_query(
        world,
        QueryRequest {
            .components = {reflected_name<Position>()},
            .with = {reflected_name<Velocity>()},
            .without = {reflected_name<Hidden>()},
            .limit = 1,
        }
    );
    REQUIRE(response);

    auto json = nlohmann::json::parse(*response);
    REQUIRE(json.at("matched") == 2);
    REQUIRE(json.at("returned") == 1);
    REQUIRE(json.at("truncated") == true);
    REQUIRE(json.at("columns").size() == 1);
    REQUIRE(json.at("columns").at(0).at("name") == reflected_name<Position>());
    REQUIRE(json.at("rows").size() == 1);
    REQUIRE(json.at("rows").at(0).at("entity") == first);
    const auto& position =
        json.at("rows").at(0).at("components").at(reflected_name<Position>());
    REQUIRE(position.at("x") == 1);
    REQUIRE(position.at("y") == 2);

    REQUIRE(first < last);
}

TEST_CASE(
    "ECS queries include DevTools-owned entities",
    "[devtools][ecs][query]"
) {
    using namespace ecs_query_test;
    register_query_test_types();

    World world;
    const auto user = world.entity();
    const auto internal = world.entity();
    world.add_component(
        internal,
        Capability {.id = "fixture", .label = "Fixture"}
    );

    auto response = execute_query(world, QueryRequest {.limit = 10});
    REQUIRE(response);
    auto json = nlohmann::json::parse(*response);
    REQUIRE(json.at("matched") == 2);
    REQUIRE(json.at("rows").at(0).at("entity") == user);
    REQUIRE(json.at("rows").at(1).at("entity") == internal);
}

TEST_CASE(
    "ECS queries validate selectors and component serialization",
    "[devtools][ecs][query]"
) {
    using namespace ecs_query_test;
    register_query_test_types();

    World world;
    const auto entity = world.entity();
    world.add_component(entity, Opaque {.value = 7});

    auto bad_limit = execute_query(world, QueryRequest {.limit = 0});
    REQUIRE_FALSE(bad_limit);
    REQUIRE(bad_limit.error().status == 400);

    auto missing = execute_query(
        world,
        QueryRequest {
            .components = {"MissingComponent"},
            .limit = 10,
        }
    );
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().status == 404);

    auto conflict = execute_query(
        world,
        QueryRequest {
            .components = {reflected_name<Position>()},
            .without = {reflected_name<Position>()},
            .limit = 10,
        }
    );
    REQUIRE_FALSE(conflict);
    REQUIRE(conflict.error().status == 400);

    auto unsupported = execute_query(
        world,
        QueryRequest {
            .components = {reflected_name<Opaque>()},
            .limit = 10,
        }
    );
    REQUIRE_FALSE(unsupported);
    REQUIRE(unsupported.error().status == 422);
    REQUIRE(
        unsupported.error().message.find("Failed to serialize component") !=
        std::string::npos
    );
}

TEST_CASE(
    "ECS entity inspection reports every component independently",
    "[devtools][ecs][inspect]"
) {
    using namespace ecs_query_test;
    register_query_test_types();

    World world;
    const auto entity = world.entity();
    world.add_component(entity, Position {.x = 2, .y = 4});
    world.add_component(entity, Opaque {.value = 7});

    const auto expected_tick = world.read_change_tick();
    auto response =
        inspect_entity(world, EntityInspectRequest {.entity = entity});
    REQUIRE(response);

    auto json = nlohmann::json::parse(*response);
    REQUIRE(json.at("observed_tick") == expected_tick);
    REQUIRE(json.at("entity") == entity);
    REQUIRE(json.at("archetype_id") > 0);
    REQUIRE(json.at("component_count") == 2);

    const auto& components = json.at("components");
    auto find_component = [&components](const std::string& name) {
        return std::ranges::find_if(components, [&name](const auto& component) {
            return component.at("name") == name;
        });
    };

    auto position = find_component(reflected_name<Position>());
    REQUIRE(position != components.end());
    REQUIRE(position->at("serialized") == true);
    REQUIRE(position->at("error").is_null());
    REQUIRE(position->at("value").at("x") == 2);
    REQUIRE(position->at("value").at("y") == 4);
    REQUIRE(position->at("added_tick") <= expected_tick);
    REQUIRE(position->at("changed_tick") <= expected_tick);

    auto opaque = find_component(reflected_name<Opaque>());
    REQUIRE(opaque != components.end());
    REQUIRE(opaque->at("serialized") == false);
    REQUIRE(opaque->at("value").is_null());
    REQUIRE(opaque->at("error").is_string());

    world.despawn(entity);
    auto missing =
        inspect_entity(world, EntityInspectRequest {.entity = entity});
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().status == 404);
}

TEST_CASE(
    "ECS provider declares and serves its manifest-driven capabilities",
    "[devtools][ecs][capability]"
) {
    using namespace ecs_query_test;
    register_query_test_types();

    App app;
    app.add_plugin<ReflectionPlugin>();
    app.add_resource(Bridge {});
    app.add_plugin<ProviderPlugin>();

    bool query_declared = false;
    bool inspect_declared = false;
    app.world().run_system_once(
        [&query_declared, &inspect_declared](
            Query<const Capability, const JsonProtocol> capabilities
        ) {
            for (auto [capability, protocol] : capabilities) {
                if (capability.id == "ecs.query") {
                    query_declared = true;
                    REQUIRE(protocol.schema == "ecs.query.v1");
                    REQUIRE(protocol.request_type);
                    REQUIRE(*protocol.request_type == type_id<QueryRequest>());
                    REQUIRE_FALSE(protocol.response_type);
                } else if (capability.id == "ecs.entity.inspect") {
                    inspect_declared = true;
                    REQUIRE(protocol.schema == "ecs.entity.inspect.v1");
                    REQUIRE(protocol.request_type);
                    REQUIRE(
                        *protocol.request_type ==
                        type_id<EntityInspectRequest>()
                    );
                    REQUIRE_FALSE(protocol.response_type);
                }
            }
        }
    );
    REQUIRE(query_declared);
    REQUIRE(inspect_declared);

    const auto user = app.world().entity();
    app.world().add_component(user, Position {.x = 2, .y = 4});

    QueryRequest body {
        .components = {reflected_name<Position>()},
        .limit = 10,
    };
    auto request_json = encode_json(Ref(body));
    REQUIRE(request_json);

    const auto request_entity = app.world().entity();
    app.world().add_component(
        request_entity,
        Request {.token = 42, .capability = "ecs.query"}
    );
    app.world().add_component(
        request_entity,
        JsonRequest {.body = std::move(*request_json)}
    );

    app.run_schedule(PostUpdate);
    REQUIRE(app.world().has_component<JsonResponse>(request_entity));
    const auto& response =
        app.world().get_component<JsonResponse>(request_entity);
    REQUIRE(response.token == 42);
    REQUIRE(response.capability == "ecs.query");
    auto json = nlohmann::json::parse(response.json);
    REQUIRE(json.at("matched") == 1);
    REQUIRE(json.at("rows").at(0).at("entity") == user);

    EntityInspectRequest inspect_body {.entity = user};
    auto inspect_json = encode_json(Ref(inspect_body));
    REQUIRE(inspect_json);
    const auto inspect_request_entity = app.world().entity();
    app.world().add_component(
        inspect_request_entity,
        Request {.token = 43, .capability = "ecs.entity.inspect"}
    );
    app.world().add_component(
        inspect_request_entity,
        JsonRequest {.body = std::move(*inspect_json)}
    );

    app.run_schedule(PostUpdate);
    REQUIRE(app.world().has_component<JsonResponse>(inspect_request_entity));
    const auto& inspect_response =
        app.world().get_component<JsonResponse>(inspect_request_entity);
    REQUIRE(inspect_response.token == 43);
    REQUIRE(inspect_response.capability == "ecs.entity.inspect");
    auto inspected = nlohmann::json::parse(inspect_response.json);
    REQUIRE(inspected.at("entity") == user);
    REQUIRE(inspected.at("component_count") == 1);
    REQUIRE(inspected.at("components").at(0).at("serialized") == true);
}
