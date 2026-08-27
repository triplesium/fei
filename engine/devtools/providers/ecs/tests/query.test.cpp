#include "ecs/query.hpp"

#include "app/app.hpp"
#include "devtools/bridge.hpp"
#include "devtools/json.hpp"
#include "devtools/types.hpp"
#include "devtools_ecs/plugin.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "runtime_inspection_ecs/entity.hpp"
#include "runtime_inspection_ecs/query.hpp"
#include "runtime_inspection_ecs/world_summary.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

using namespace ets;
using namespace ets::devtools;
using namespace ets::devtools::ecs;
using ets::runtime_inspection::ecs::EntityInspectRequest;
using ets::runtime_inspection::ecs::QueryInspectionProvider;
using ets::runtime_inspection::ecs::QueryRequest;
using ets::runtime_inspection::ecs::WorldSummaryInspectionProvider;
using ets::runtime_inspection::ecs::WorldSummaryRequest;

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
    "ECS provider declares and serves its manifest-driven capabilities",
    "[devtools][ecs][capability]"
) {
    using namespace ecs_query_test;
    register_query_test_types();

    App app;
    app.add_resource(Bridge {});
    app.add_plugin<ProviderPlugin>();
    app.finish();

    bool query_declared = false;
    bool inspect_declared = false;
    bool summary_declared = false;
    app.world().run_system_once(
        [&query_declared, &inspect_declared, &summary_declared](
            Query<const Capability, const JsonProtocol> capabilities
        ) {
            for (auto [capability, protocol] : capabilities) {
                if (capability.id == "ecs.query") {
                    query_declared = true;
                    REQUIRE(protocol.schema == QueryInspectionProvider::schema);
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
                } else if (capability.id == "ecs.world.summary") {
                    summary_declared = true;
                    REQUIRE(
                        protocol.schema ==
                        WorldSummaryInspectionProvider::schema
                    );
                    REQUIRE(protocol.request_type);
                    REQUIRE(
                        *protocol.request_type == type_id<WorldSummaryRequest>()
                    );
                    REQUIRE_FALSE(protocol.response_type);
                }
            }
        }
    );
    REQUIRE(query_declared);
    REQUIRE(inspect_declared);
    REQUIRE(summary_declared);

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
    REQUIRE(json.at("rows").at(0).at("entity") == user.value);

    EntityInspectRequest inspect_body {.entity = user};
    auto inspect_json = encode_json(Ref(inspect_body));
    if (!inspect_json) {
        FAIL(inspect_json.error());
    }
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
    if (app.world().has_component<ErrorResponse>(inspect_request_entity)) {
        FAIL(app.world()
                 .get_component<ErrorResponse>(inspect_request_entity)
                 .message);
    }
    REQUIRE(app.world().has_component<JsonResponse>(inspect_request_entity));
    const auto& inspect_response =
        app.world().get_component<JsonResponse>(inspect_request_entity);
    REQUIRE(inspect_response.token == 43);
    REQUIRE(inspect_response.capability == "ecs.entity.inspect");
    auto inspected = nlohmann::json::parse(inspect_response.json);
    REQUIRE(inspected.at("entity") == user.value);
    REQUIRE(inspected.at("component_count") == 1);
    REQUIRE(inspected.at("components").at(0).at("serialized") == true);

    WorldSummaryRequest summary_body {
        .archetype_limit = 128,
        .include_empty_archetypes = false,
    };
    auto summary_json = encode_json(Ref(summary_body));
    REQUIRE(summary_json);
    const auto summary_request_entity = app.world().entity();
    app.world().add_component(
        summary_request_entity,
        Request {.token = 44, .capability = "ecs.world.summary"}
    );
    app.world().add_component(
        summary_request_entity,
        JsonRequest {.body = std::move(*summary_json)}
    );

    app.run_schedule(PostUpdate);
    REQUIRE(app.world().has_component<JsonResponse>(summary_request_entity));
    const auto& summary_response =
        app.world().get_component<JsonResponse>(summary_request_entity);
    REQUIRE(summary_response.token == 44);
    REQUIRE(summary_response.capability == "ecs.world.summary");
    auto summarized = nlohmann::json::parse(summary_response.json);
    REQUIRE(summarized.at("entity_count").get<uint64>() >= 1);
    REQUIRE(summarized.at("known_archetype_count").get<uint64>() >= 1);
}
