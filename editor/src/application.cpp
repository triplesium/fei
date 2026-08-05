#include "application.hpp"

#include "app/reflection_plugin.hpp"
#include "base/env.hpp"
#include "core/plugin.hpp"
#include "editor/plugin.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "imgui/plugin.hpp"
#include "project/plugin.hpp"
#include "rendering/plugin.hpp"
#include "sprite/plugin.hpp"
#include "window/window.hpp"

#include <chrono>
#include <cstdint>
#include <utility>

namespace fei::editor {

EditorApplication::EditorApplication(Project project) {
    m_app.add_resource(
        WindowConfig {
            .width = 1600,
            .height = 900,
            .title = "Fei Editor",
        }
    );
    m_app.add_plugin(ProjectPlugin {std::move(project)})
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
        .add_plugin(EditorPlugin {});
}

EditorApplication::~EditorApplication() {
    shutdown();
}

void EditorApplication::shutdown() noexcept {
    m_editor_layer.shutdown(m_app.world());
    m_app.shutdown();
}

void EditorApplication::update_frame() {
    m_app.update();
    m_editor_layer.draw(m_app.world());
    m_app.render();
}

void EditorApplication::run() {
    if (m_app.lifecycle() == AppLifecycle::Stopped) {
        return;
    }

    const auto exit_after_seconds =
        read_environment_variable<double>("FEI_EXIT_AFTER_SECONDS");
    const auto exit_after_frames =
        read_environment_variable<std::uint64_t>("FEI_EXIT_AFTER_FRAMES");
    const auto start_time = std::chrono::steady_clock::now();
    std::uint64_t frame_count = 0;

    try {
        m_app.startup();
        bool should_stop = false;
        while (!should_stop) {
            update_frame();
            ++frame_count;

            auto& app_states = m_app.resource<AppStates>();
            if (exit_after_frames && frame_count >= *exit_after_frames) {
                app_states.should_stop = true;
            }
            if (exit_after_seconds) {
                const auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start_time
                );
                if (elapsed.count() >= *exit_after_seconds) {
                    app_states.should_stop = true;
                }
            }
            should_stop = app_states.should_stop;
        }
    } catch (...) {
        shutdown();
        throw;
    }
    shutdown();
}

} // namespace fei::editor
