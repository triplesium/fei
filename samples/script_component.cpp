#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_params.hpp"
#include "scripting/component.hpp"
#include "scripting/plugin.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

#include <print>

using namespace fei;

void setup(Res<AssetServer> asset_server, Commands commands) {
    commands.spawn().add(
        ScriptComponent {
            .script = asset_server->load<ScriptAsset>("camera_control.lua")
        },
        Transform3d {
            .position = {0.0f, 0.0f, 5.0f},
        }
    );
}

int main() {
    App()
        .add_plugins(
            AssetsPlugin {},
            WindowPlugin {},
            TimePlugin {},
            InputPlugin {},
            ScriptingPlugin {}
        )
        .add_systems(StartUp, setup)
        .run();

    return 0;
}
