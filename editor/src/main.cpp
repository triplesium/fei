#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/plugin.hpp"
#include "core/plugin.hpp"
#include "editor/plugin.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "imgui/plugin.hpp"
#include "rendering/plugin.hpp"
#include "sprite/plugin.hpp"
#include "window/window.hpp"

using namespace fei;

int main() {
    App app;
    app.add_resource(
        WindowConfig {
            .width = 1600,
            .height = 900,
            .title = "Fei Editor",
        }
    );
    app.add_plugin<AssetsPlugin>()
        .add_plugin<OpenGLGlfwPlugin>()
        .add_plugin<CorePlugin>()
        .add_plugin<RenderingPlugin>()
        .add_plugin(
            SpritePlugin {
                SpritePluginConfig {
                    .output = SpriteOutputMode::Texture,
                    .width = 1280,
                    .height = 720,
                },
            }
        )
        .add_plugin(ImGuiPlugin {ImGuiPluginConfig {.docking = true}})
        .add_plugin<ReflectionPlugin>()
        .add_plugin(editor::EditorPlugin {})
        .run();
    return 0;
}
