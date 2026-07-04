#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/resource_traits.hpp"
#include "ecs/system_params.hpp"

#include <string>
#include <vector>

struct GLFWwindow;

namespace fei {

struct Window {
    GLFWwindow* glfw_window;
    int width, height;
};

struct GlfwWindowHint {
    int hint {0};
    int value {0};
};

struct WindowConfig {
    int width {1920};
    int height {1080};
    std::string title {"Fei Engine"};
    std::vector<GlfwWindowHint> hints;
};

template<>
struct ResourceTraits<Window> {
    static constexpr bool main_thread_only = true;
};

GLFWwindow* setup_glfw_window(const WindowConfig& config);

void window_prepare(ResRW<Window> win_res);
void update_should_close(ResRO<Window> win_res, ResRW<AppStates> app_states);

class WindowPlugin : public Plugin {
  public:
    void setup(App& app) override {
        if (!app.has_resource<WindowConfig>()) {
            app.add_resource(WindowConfig {});
        }

        auto& config = app.resource<WindowConfig>();
        app.add_resource(
            Window {
                .glfw_window = setup_glfw_window(config),
                .width = config.width,
                .height = config.height,
            }
        );
        app.add_systems(First, window_prepare);
        app.add_systems(Last, update_should_close);
    }
};

} // namespace fei
