#include "project_scripting_luau/playtest.hpp"

#include "app/app.hpp"
#include "runtime_protocol/playtest.hpp"
#include "scripting/runtime.hpp"
#include "scripting/script_system_registry.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ets::project_runtime {
namespace {

using runtime_protocol::PlaytestError;
using runtime_protocol::PlaytestErrorKind;

PlaytestError playtest_error(LuauScriptError error, PlaytestErrorKind kind) {
    return PlaytestError {
        .kind = kind,
        .message = std::move(error.message),
    };
}

} // namespace

void LuauPlaytestsPlugin::setup(App& app) {
    if (!app.has_resource<runtime_protocol::PlaytestRegistry>()) {
        throw std::runtime_error(
            "LuauPlaytestsPlugin requires a PlaytestRegistry resource"
        );
    }

    auto& registry = app.resource<runtime_protocol::PlaytestRegistry>();
    auto& runtime = app.resource<LuauRuntime>();
    for (const auto& script_module :
         app.resource<LuauScriptSystemRegistry>().modules()) {
        const auto declarations =
            runtime.module_playtests(script_module.module);
        for (std::size_t index = 0; index < declarations.size(); ++index) {
            const auto& declaration = declarations[index];
            const auto module = script_module.module;
            auto registered = registry.add(
                runtime_protocol::PlaytestInterfaceRegistration {
                    .descriptor =
                        runtime_protocol::PlaytestInterfaceDescriptor {
                            .id = declaration.id,
                            .label = declaration.label,
                            .description = declaration.description,
                            .decision_ticks = declaration.decision_ticks,
                            .minimum_ticks = declaration.minimum_ticks,
                            .maximum_ticks = declaration.maximum_ticks,
                            .allow_tick_override =
                                declaration.allow_tick_override,
                            .action_schema_json =
                                declaration.action_schema_json,
                            .observation_schema_json =
                                declaration.observation_schema_json,
                        },
                    .begin_step = [module, index](
                                      World& world,
                                      std::string_view action_json
                                  ) -> Status<PlaytestError> {
                        auto status = world.resource<LuauRuntime>()
                                          .begin_module_playtest_step(
                                              module,
                                              index,
                                              world,
                                              action_json
                                          );
                        if (!status) {
                            return failure(playtest_error(
                                std::move(status.error()),
                                PlaytestErrorKind::InvalidAction
                            ));
                        }
                        return {};
                    },
                    .end_step = [module,
                                 index](World& world) -> Status<PlaytestError> {
                        auto status =
                            world.resource<LuauRuntime>()
                                .end_module_playtest_step(module, index, world);
                        if (!status) {
                            return failure(playtest_error(
                                std::move(status.error()),
                                PlaytestErrorKind::Internal
                            ));
                        }
                        return {};
                    },
                    .observe = [module, index](World& world)
                        -> Result<std::string, PlaytestError> {
                        auto observation =
                            world.resource<LuauRuntime>()
                                .observe_module_playtest(module, index, world);
                        if (!observation) {
                            return failure(playtest_error(
                                std::move(observation.error()),
                                PlaytestErrorKind::Internal
                            ));
                        }
                        return std::move(*observation);
                    },
                }
            );
            if (!registered) {
                throw std::runtime_error(
                    "Failed to register Luau Plugin playtest interface: " +
                    registered.error().message
                );
            }
        }
    }
}

} // namespace ets::project_runtime
