#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "core/plugin.hpp"
#include "core/text.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics_webgpu_browser/plugin.hpp"
#include "input/input.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "sprite/components.hpp"
#include "sprite/plugin.hpp"
#include "sprite/renderer.hpp"
#include "window_browser/input.hpp"

#include <algorithm>
#include <cmath>
#include <emscripten.h>
#include <string_view>

namespace fei::browser_sample {
namespace {

struct BrowserSprite {};

struct BrowserPresentation {
    bool presented {false};
};

void set_browser_status(const char* status) {
    EM_ASM(
        {
            const status = UTF8ToString($0);
            document.documentElement.dataset.feiStatus = status;
            console.log("[fei] " + status);
        },
        status
    );
}

void set_browser_input_status(const char* status, float sprite_x) {
    EM_ASM(
        {
            document.documentElement.dataset.feiInput = UTF8ToString($0);
            document.documentElement.dataset.feiSpriteX = $1.toString();
        },
        status,
        sprite_x
    );
}

void setup_browser_scene(
    ResRW<AssetServer> asset_server,
    ResRO<Assets<TextAsset>> text_assets,
    Commands commands
) {
    set_browser_status("loading browser assets");
    const auto ready_asset = asset_server->load<TextAsset>("browser/ready.txt");
    const auto ready_text = text_assets->get(ready_asset);
    if (!ready_text || !std::string_view(ready_text->text())
                            .starts_with("fei browser assets ready")) {
        set_browser_status("browser asset loading failed");
        error("Browser readiness asset was unavailable or invalid");
        return;
    }

    const auto image = asset_server->load<Image>("browser/checker.ppm");
    commands.spawn().add(
        Camera2d {
            .vertical_size = 5.0F,
            .clear_color = {0.08F, 0.12F, 0.2F, 1.0F},
        },
        Transform2d {}
    );
    commands.spawn().add(
        Sprite {
            .image = image,
            .size = {2.5F, 2.5F},
        },
        Transform2d {},
        BrowserSprite {}
    );
    set_browser_status("sprite scene loaded from VFS");
}

void animate_browser_sprite(
    Query<Transform2d>::Filter<With<BrowserSprite>> query,
    ResRO<Time> time,
    ResRO<KeyInput> keys,
    ResRO<MouseInput> mouse,
    ResRO<MouseScrollInput> scroll
) {
    for (auto [transform] : query) {
        float direction = 0.0F;
        if (keys->pressed(KeyCode::Left) || keys->pressed(KeyCode::A)) {
            direction -= 1.0F;
        }
        if (keys->pressed(KeyCode::Right) || keys->pressed(KeyCode::D)) {
            direction += 1.0F;
        }
        transform->position.x = std::clamp(
            transform->position.x + direction * time->delta() * 1.5F,
            -2.5F,
            2.5F
        );
        transform->position.y = std::sin(time->elapsed_time()) * 0.2F;
        transform->rotation = time->elapsed_time() * 35.0F;

        if (keys->just_pressed(KeyCode::Right)) {
            set_browser_input_status("Right:pressed", transform->position.x);
        }
        if (keys->just_released(KeyCode::Right)) {
            set_browser_input_status("Right:released", transform->position.x);
        }
        if (mouse->just_pressed(MouseButton::Left)) {
            set_browser_input_status(
                "MouseLeft:pressed",
                transform->position.x
            );
        }
        if (mouse->just_released(MouseButton::Left)) {
            set_browser_input_status(
                "MouseLeft:released",
                transform->position.x
            );
        }
        if (scroll->delta() != Vector2::Zero) {
            set_browser_input_status("Wheel", transform->position.x);
        }
    }
}

void report_sprite_presented(
    ResRO<SpritePhase> phase,
    ResRW<BrowserPresentation> presentation
) {
    if (presentation->presented || !phase->active || phase->batches.empty()) {
        return;
    }
    presentation->presented = true;
    set_browser_status("sprite pipeline presented");
}

class BrowserSamplePlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<WebGpuBrowserPlugin>()
            .require<BrowserInputPlugin>()
            .require<CorePlugin>()
            .require<SpritePlugin>();
    }

    void setup(App& app) override {
        app.add_systems(PreStartUp, setup_browser_scene)
            .add_systems(Update, animate_browser_sprite);
        app.sub_app<RenderApp>()
            .add_resource(BrowserPresentation {})
            .add_systems(
                RenderUpdate,
                report_sprite_presented | in_set<RenderingSystems::Submit>()
            );
    }
};

} // namespace
} // namespace fei::browser_sample

int main() {
    using namespace fei;
    using namespace fei::browser_sample;

    App app;
    app.add_plugin<BrowserSamplePlugin>();
    app.run();
    return 0;
}
