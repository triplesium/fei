#include "devtools_input/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "input_commands.hpp"
#include "refl/registry.hpp"
#include "window/input.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace fei::devtools::input {

namespace {

struct InputState {
    std::vector<KeyCode> pressed;

    bool set_key(KeyCode key, bool down) {
        if (key == KeyCode::Unknown) {
            return false;
        }

        auto iter = std::find(pressed.begin(), pressed.end(), key);
        if (down) {
            if (iter == pressed.end()) {
                pressed.push_back(key);
            }
            return true;
        }

        if (iter != pressed.end()) {
            pressed.erase(iter);
        }
        return true;
    }

    void clear() { pressed.clear(); }
};

void apply_input_commands(
    Query<Entity, const Request, const CommandRequest> requests,
    ResRW<InputState> input_state,
    Commands commands
) {
    for (auto [entity, request, command] : requests) {
        if (request.capability == "input.clear") {
            auto clear_command = validate_clear_command_body(command.body);
            if (!clear_command) {
                commands.entity(entity).add(
                    ErrorResponse {
                        .token = request.token,
                        .capability = request.capability,
                        .status = 400,
                        .message = std::move(clear_command.error()),
                    }
                );
                continue;
            }

            input_state->clear();
            auto response = clear_command_response_json();
            if (!response) {
                commands.entity(entity).add(
                    ErrorResponse {
                        .token = request.token,
                        .capability = request.capability,
                        .status = 500,
                        .message = std::move(response.error()),
                    }
                );
                continue;
            }
            commands.entity(entity).add(
                CommandResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .json = std::move(*response),
                }
            );
            continue;
        }
        if (request.capability != "input.key") {
            continue;
        }

        auto key_command = parse_key_command_body(command.body);
        if (!key_command) {
            commands.entity(entity).add(
                ErrorResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .status = 400,
                    .message = std::move(key_command.error()),
                }
            );
            continue;
        }
        if (!input_state->set_key(key_command->key, key_command->down)) {
            commands.entity(entity).add(
                ErrorResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .status = 400,
                    .message = "Unsupported key code",
                }
            );
            continue;
        }
        auto response = key_command_response_json(*key_command);
        if (!response) {
            commands.entity(entity).add(
                ErrorResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .status = 500,
                    .message = std::move(response.error()),
                }
            );
            continue;
        }
        commands.entity(entity).add(
            CommandResponse {
                .token = request.token,
                .capability = request.capability,
                .json = std::move(*response),
            }
        );
    }
}

void apply_pressed_keys(ResRO<InputState> input_state, ResRW<KeyInput> input) {
    for (auto key : input_state->pressed) {
        input->press(key);
    }
}

} // namespace

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<Bridge>()) {
        fatal(
            "devtools::input::ProviderPlugin requires devtools::CorePlugin. "
            "Add devtools::CorePlugin before devtools::input::ProviderPlugin."
        );
    }
    if (!app.has_resource<KeyInput>()) {
        fatal(
            "devtools::input::ProviderPlugin requires InputPlugin. Add "
            "InputPlugin before devtools::input::ProviderPlugin."
        );
    }

    auto& registry = Registry::instance();
    if (!registry.has_enum(type_id<KeyCode>()) ||
        !registry.try_get_cls(type_id<KeyCommandBody>()) ||
        !registry.try_get_cls(type_id<KeyCommandResponse>()) ||
        !registry.try_get_cls(type_id<ClearCommandBody>()) ||
        !registry.try_get_cls(type_id<ClearCommandResponse>())) {
        fatal(
            "devtools::input::ProviderPlugin requires ReflectionPlugin. Add "
            "ReflectionPlugin before devtools::input::ProviderPlugin."
        );
    }

    declare_capability(
        app.world(),
        "input.key",
        "Keyboard Key",
        CommandCapability {
            .schema = "input.key.v1",
            .request_type = type_id<KeyCommandBody>(),
            .response_type = type_id<KeyCommandResponse>(),
        }
    );
    declare_capability(
        app.world(),
        "input.clear",
        "Clear Input",
        CommandCapability {
            .schema = "input.clear.v1",
            .request_type = type_id<ClearCommandBody>(),
            .response_type = type_id<ClearCommandResponse>(),
        }
    );

    app.add_resource(InputState {});
    app.add_systems(Update, apply_input_commands);
    app.add_systems(PreUpdate, apply_pressed_keys | after(key_input_system));
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::input
