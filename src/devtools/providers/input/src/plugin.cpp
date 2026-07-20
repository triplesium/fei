#include "devtools_input/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "input_types.hpp"
#include "window/input.hpp"

#include <algorithm>
#include <string_view>
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

struct KeyboardKey {
    using RequestBody = KeyInputRequest;
    using ResponseBody = KeyInputResponse;

    static constexpr std::string_view id {"input.key"};
    static constexpr std::string_view label {"Keyboard Key"};
    static constexpr std::string_view schema {"input.key.v1"};
    static constexpr ScheduleId schedule {Update};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        ResRW<InputState> input_state,
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
            if (!input_state->set_key(body->key, body->down)) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    400,
                    "Unsupported key code"
                );
                continue;
            }

            respond_capability(
                commands,
                entity,
                request,
                ResponseBody {
                    .ok = true,
                    .key = body->key,
                    .down = body->down,
                }
            );
        }
    }
};

struct ClearInput {
    using RequestBody = void;
    using ResponseBody = ClearInputResponse;

    static constexpr std::string_view id {"input.clear"};
    static constexpr std::string_view label {"Clear Input"};
    static constexpr std::string_view schema {"input.clear.v1"};
    static constexpr ScheduleId schedule {Update};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        ResRW<InputState> input_state,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            input_state->clear();
            respond_capability(commands, entity, request, ResponseBody {});
        }
    }
};

void apply_pressed_keys(ResRO<InputState> input_state, ResRW<KeyInput> input) {
    for (auto key : input_state->pressed) {
        input->press(key);
    }
}

} // namespace

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<KeyInput>()) {
        fatal(
            "devtools::input::ProviderPlugin requires InputPlugin. Add "
            "InputPlugin before devtools::input::ProviderPlugin."
        );
    }

    add_capabilities<KeyboardKey, ClearInput>(app);

    app.add_resource(InputState {});
    app.configure_sets(
           PreUpdate,
           chain(InputSystems::Update {}, InputSystems::ApplyDevtools {})
    )
        .add_systems(
            PreUpdate,
            apply_pressed_keys | in_set<InputSystems::ApplyDevtools>()
        );
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::input
