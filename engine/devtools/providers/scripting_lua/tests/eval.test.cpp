#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/plugin.hpp"
#include "devtools/bridge.hpp"
#include "devtools/json.hpp"
#include "devtools/types.hpp"
#include "devtools_scripting_lua/plugin.hpp"
#include "eval_types.hpp"
#include "scripting_lua/plugin.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::devtools;
using namespace fei::devtools::scripting_lua;

namespace {

Entity queue_eval_request(App& app, Token token, std::string source) {
    EvalRequest body {.source = std::move(source)};
    auto json = encode_json(Ref(body));
    REQUIRE(json);

    auto entity = app.world().entity();
    app.world().add_component(
        entity,
        Request {
            .token = token,
            .capability = "lua.eval",
            .deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds {1},
        }
    );
    app.world().add_component(entity, JsonRequest {.body = std::move(*json)});
    return entity;
}

void configure_eval_app(App& app, devtools::scripting_lua::Config config = {}) {
    app.add_plugin<AssetsPlugin>()
        .add_plugin<ReflectionPlugin>()
        .add_plugin<LuaScriptingPlugin>()
        .add_resource(Bridge {})
        .add_plugin(ProviderPlugin {config});
}

EvalResponse response_for(World& world, Entity entity) {
    REQUIRE(world.has_component<JsonResponse>(entity));
    const auto& response =
        static_cast<const World&>(world).get_component<JsonResponse>(entity);
    auto decoded = decode_json<EvalResponse>(response.json);
    REQUIRE(decoded);
    return std::move(*decoded);
}

} // namespace

TEST_CASE(
    "lua.eval executes with exclusive world access and captured output",
    "[devtools][lua][eval]"
) {
    App app;
    configure_eval_app(app);
    auto request = queue_eval_request(
        app,
        7,
        R"(
            local states = world:resource(AppStates)
            print("should_stop", states.should_stop)
            states.should_stop = true
        )"
    );

    app.run_schedule(Update);

    auto response = response_for(app.world(), request);
    REQUIRE(response.ok);
    REQUIRE(response.error.empty());
    REQUIRE(response.output == std::vector<std::string> {"should_stop\tfalse"});
    REQUIRE_FALSE(response.truncated);
    REQUIRE(app.resource<AppStates>().should_stop);
}

TEST_CASE(
    "lua.eval returns output alongside script errors",
    "[devtools][lua][eval][error]"
) {
    App app;
    configure_eval_app(app);
    auto request = queue_eval_request(app, 8, "print('before')\nerror('boom')");

    app.run_schedule(Update);

    auto response = response_for(app.world(), request);
    REQUIRE_FALSE(response.ok);
    REQUIRE(response.output == std::vector<std::string> {"before"});
    REQUIRE(response.error.find("boom") != std::string::npos);
}

TEST_CASE(
    "lua.eval rejects source beyond its configured limit",
    "[devtools][lua][eval][limits]"
) {
    App app;
    configure_eval_app(
        app,
        devtools::scripting_lua::Config {.max_source_bytes = 4}
    );
    auto request = queue_eval_request(app, 9, "12345");

    app.run_schedule(Update);

    REQUIRE(app.world().has_component<ErrorResponse>(request));
    const auto& response = static_cast<const World&>(app.world())
                               .get_component<ErrorResponse>(request);
    REQUIRE(response.status == 413);
    REQUIRE(response.message.find("4 bytes") != std::string::npos);
}
