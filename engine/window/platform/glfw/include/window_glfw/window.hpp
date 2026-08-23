#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/resource_traits.hpp"
#include "ecs/system_params.hpp"
#include "window/window.hpp"

#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace ets {

struct GlfwWindow {
    GLFWwindow* handle {nullptr};
};

struct GlfwWindowHint {
    int hint {0};
    int value {0};
};

struct GlfwWindowConfig {
    int width {1920};
    int height {1080};
    std::string title {"Entisium Engine"};
    std::vector<GlfwWindowHint> hints;
};

template<>
struct ResourceTraits<GlfwWindow> {
    static constexpr bool main_thread_only = true;
};

GLFWwindow* setup_glfw_window(const GlfwWindowConfig& config);

void prepare_glfw_window(ResRO<GlfwWindow> glfw, ResRW<Window> window);
void update_should_close(ResRO<GlfwWindow> glfw, ResRW<AppStates> app_states);

ETS_REFLECT(Plugin)
class GlfwWindowPlugin : public Plugin {
  private:
    std::vector<GlfwWindowHint> m_hints;

  public:
    explicit GlfwWindowPlugin(std::vector<GlfwWindowHint> hints = {}) :
        m_hints(std::move(hints)) {}

    void setup(App& app) override {
        if (!app.has_resource<GlfwWindowConfig>()) {
            app.add_resource(GlfwWindowConfig {});
        }

        auto& config = app.resource<GlfwWindowConfig>();
        config.hints.insert(config.hints.end(), m_hints.begin(), m_hints.end());
        app.add_resource(GlfwWindow {.handle = setup_glfw_window(config)})
            .add_resource(
                Window {
                    .width = config.width,
                    .height = config.height,
                }
            );
        app.add_systems(
            First,
            prepare_glfw_window | in_set<WindowSystems::Prepare>()
        );
        app.add_systems(Last, update_should_close);
    }

    void cleanup(App& app) noexcept override;
};

} // namespace ets
