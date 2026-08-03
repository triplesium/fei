#include "eval.hpp"

#include "devtools/json.hpp"
#include "ecs/dynamic/system.hpp"
#include "ecs/dynamic/world.hpp"
#include "ecs/world.hpp"
#include "scripting_lua/runtime.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fei::devtools::scripting_lua {
namespace {

class LuaEvalExecutor final : public DynamicSystemExecutor {
  public:
    LuaEvalExecutor(
        Entity request_entity,
        Token token,
        std::string capability,
        EvalRequest request,
        EvalLimits limits
    ) :
        m_request_entity(request_entity), m_token(token),
        m_capability(std::move(capability)), m_request(std::move(request)),
        m_limits(limits) {}

    Status<DynamicSystemError> execute(const std::vector<Ref>& args) override {
        if (args.size() != 1) {
            return failure(
                DynamicSystemError {
                    "DevTools Lua eval expected exactly one world parameter"
                }
            );
        }

        auto* context = args.front().try_get<DynamicWorld>();
        if (!context || !context->active()) {
            return failure(
                DynamicSystemError {
                    "DevTools Lua eval received an inactive world parameter"
                }
            );
        }

        auto& world = context->world();
        if (!world.has_resource<LuaRuntime>()) {
            complete_error(world, 500, "LuaRuntime resource is unavailable");
            return {};
        }

        std::array globals {
            LuaEvalGlobal {
                .name = "world",
                .value = Ref(*context),
            },
        };
        auto result = world.resource<LuaRuntime>().eval_script(
            LuaScriptSource {
                .name = "devtools.lua",
                .content = std::move(m_request.source),
            },
            globals,
            LuaEvalOptions {
                .max_output_bytes = m_limits.max_output_bytes,
                .instruction_limit = m_limits.instruction_limit,
                .time_limit =
                    std::chrono::milliseconds {m_limits.time_limit_ms},
            }
        );

        EvalResponse response {
            .ok = result.ok,
            .output = std::move(result.output),
            .error = std::move(result.error),
            .truncated = result.truncated,
        };
        auto json = encode_json(Ref(response));
        if (!json) {
            complete_error(world, 500, std::move(json.error()));
            return {};
        }
        if (world.has_entity(m_request_entity)) {
            world.add_component(
                m_request_entity,
                JsonResponse {
                    .token = m_token,
                    .capability = m_capability,
                    .json = std::move(*json),
                }
            );
        }
        return {};
    }

  private:
    Entity m_request_entity;
    Token m_token;
    std::string m_capability;
    EvalRequest m_request;
    EvalLimits m_limits;

    void complete_error(World& world, int status, std::string message) const {
        if (!world.has_entity(m_request_entity)) {
            return;
        }
        world.add_component(
            m_request_entity,
            ErrorResponse {
                .token = m_token,
                .capability = m_capability,
                .status = status,
                .message = std::move(message),
            }
        );
    }
};

} // namespace

std::unique_ptr<System> make_eval_system(
    Entity request_entity,
    Token token,
    std::string capability,
    EvalRequest request,
    EvalLimits limits
) {
    DynamicSystemParams params;
    params.push_back(std::make_unique<DynamicWorld>("world"));
    return std::make_unique<DynamicSystem>(
        "devtools.lua.eval",
        std::move(params),
        std::make_unique<LuaEvalExecutor>(
            request_entity,
            token,
            std::move(capability),
            std::move(request),
            limits
        )
    );
}

} // namespace fei::devtools::scripting_lua
