#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_params.hpp"
#include "scripting/component.hpp"
#include "scripting_lua/plugin.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

using namespace fei;

void setup(ResRW<AssetServer> asset_server, Commands commands) {
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
            ReflectionPlugin {},
            LuaScriptingPlugin {}
        )
        .add_systems(StartUp, setup)
        .run();

    return 0;
}
