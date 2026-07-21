#include "devtools_scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system.hpp"
#include "ecs/system_params.hpp"
#include "eval.hpp"
#include "eval_types.hpp"
#include "scripting_lua/runtime.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace fei::devtools::scripting_lua {
namespace {

struct LuaEval {
    using RequestBody = EvalRequest;
    using ResponseBody = EvalResponse;

    static constexpr std::string_view id {"lua.eval"};
    static constexpr std::string_view label {"Evaluate Lua"};
    static constexpr std::string_view schema {"lua.eval.v1"};
    static constexpr ScheduleId schedule {Update};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        ResRO<EvalLimits> limits,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            auto body = decode_capability_request<RequestBody>(json);
            if (!body) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    400,
                    std::move(body.error())
                );
                continue;
            }
            if (body->source.size() > limits->max_source_bytes) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    413,
                    "Lua source exceeds the maximum size of " +
                        std::to_string(limits->max_source_bytes) + " bytes"
                );
                continue;
            }

            auto system = make_eval_system(
                entity,
                request.token,
                request.capability,
                std::move(*body),
                *limits
            );
            commands.add_command([system =
                                      std::move(system)](World& world) mutable {
                system->run(world);
            });
        }
    }
};

} // namespace

ProviderPlugin::ProviderPlugin(Config config) : m_config(config) {}

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<LuaRuntime>()) {
        fatal(
            "devtools::scripting_lua::ProviderPlugin requires "
            "LuaScriptingPlugin. Add LuaScriptingPlugin before the DevTools "
            "Lua provider."
        );
    }

    app.add_resource(
        EvalLimits {
            .max_source_bytes = m_config.max_source_bytes,
            .max_output_bytes = m_config.max_output_bytes,
            .instruction_limit = m_config.instruction_limit,
            .time_limit_ms = m_config.time_limit_ms,
        }
    );
    add_capability<LuaEval>(app);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::scripting_lua
