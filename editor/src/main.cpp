#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "base/log.hpp"
#include "core/plugin.hpp"
#include "editor/plugin.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "imgui/plugin.hpp"
#include "project/plugin.hpp"
#include "project/project.hpp"
#include "rendering/plugin.hpp"
#include "sprite/plugin.hpp"
#include "window/window.hpp"

#include <utility>

using namespace fei;

int main(int argc, char** argv) {
    if (argc < 2) {
        error("Usage: fei-editor <path-to-project.yaml>");
        return 1;
    }

    auto project = Project::load(argv[1]);
    if (!project) {
        error(
            "Failed to load project '{}': {}",
            project.error().path.string(),
            project.error().message
        );
        return 1;
    }

    App app;
    app.add_resource(
        WindowConfig {
            .width = 1600,
            .height = 900,
            .title = "Fei Editor",
        }
    );
    app.add_plugin(ProjectPlugin {std::move(*project)})
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
