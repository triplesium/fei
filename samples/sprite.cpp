#include "render2d/sprite.hpp"
#include "app/asset.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "render2d/camera.hpp"
#include "window/window.hpp"

#include "common.hpp"

using namespace fei;

void sprite_system(
    Commands commands,
    Res<AssetServer> asset_server,
    Res<Window> res_win
) {
    commands.spawn().add(
        Camera {
            .width = (float)res_win->width,
            .height = (float)res_win->height,
            .near = -1.0f,
            .far = 1.0f,
        },
        Transform2D {
            .position = {0.0f, 0.0f},
        }
    );
    commands.spawn().add(
        Sprite {
            .texture = asset_server->load<Texture2D>("awesomeface.png"),
        },
        Transform2D {
            .position = {0.0f, 0.0f},
        }
    );
}

int main() {
    App app;
    app.add_plugin<SamplePlugin>().add_system(StartUp, sprite_system).run();

    return 0;
}
