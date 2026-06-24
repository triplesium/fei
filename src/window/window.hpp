#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/resource_traits.hpp"
#include "ecs/system_params.hpp"

#include <string>

struct GLFWwindow;

namespace fei {

struct Window {
    GLFWwindow* glfw_window;
    int width, height;
};

template<>
struct ResourceTraits<Window> {
    static constexpr bool main_thread_only = true;
};

GLFWwindow* setup_glfw(int width, int height, const std::string& title);

void window_prepare(Res<Window> win_res);
void swap_buffers(Res<Window> win_res);
void update_should_close(Res<Window> win_res, Res<AppStates> app_states);

class WindowPlugin : public Plugin {
  public:
    void setup(App& app) override {
        int width = 1920, height = 1080;
        app.add_resource(
            Window {
                .glfw_window = setup_glfw(width, height, "Fei Engine"),
                .width = width,
                .height = height,
            }
        );
        app.add_systems(First, window_prepare);
        // app.add_system(RenderEnd, swap_buffers);
        app.add_systems(Last, update_should_close);
    }
};

} // namespace fei
