#include "window/window.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace fei {

namespace {

void apply_glfw_hints(const std::vector<GlfwWindowHint>& hints) {
    glfwDefaultWindowHints();

    for (const auto& hint : hints) {
        glfwWindowHint(hint.hint, hint.value);
    }
}

} // namespace

GLFWwindow* setup_glfw_window(const WindowConfig& config) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    apply_glfw_hints(config.hints);

    GLFWwindow* win = glfwCreateWindow(
        config.width,
        config.height,
        config.title.c_str(),
        nullptr,
        nullptr
    );
    if (!win) {
        throw std::runtime_error("Failed to create window");
    }

    return win;
}

void window_prepare(ResRW<Window> win_res) {
    glfwPollEvents();
    Window& win = *win_res;
    glfwGetFramebufferSize(win.glfw_window, &win.width, &win.height);
}

void update_should_close(ResRO<Window> win_res, ResRW<AppStates> app_states) {
    if (glfwWindowShouldClose(win_res->glfw_window)) {
        app_states->should_stop = true;
    }
}

} // namespace fei
