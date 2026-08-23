#include "window_glfw/window.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace ets {

namespace {

void apply_glfw_hints(const std::vector<GlfwWindowHint>& hints) {
    glfwDefaultWindowHints();

    for (const auto& hint : hints) {
        glfwWindowHint(hint.hint, hint.value);
    }
}

} // namespace

GLFWwindow* setup_glfw_window(const GlfwWindowConfig& config) {
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

void prepare_glfw_window(ResRO<GlfwWindow> glfw, ResRW<Window> window) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfw->handle, &window->width, &window->height);
}

void update_should_close(ResRO<GlfwWindow> glfw, ResRW<AppStates> app_states) {
    if (glfwWindowShouldClose(glfw->handle)) {
        app_states->should_stop = true;
    }
}

void GlfwWindowPlugin::cleanup(App& app) noexcept {
    if (!app.has_resource<GlfwWindow>()) {
        return;
    }

    auto& window = app.resource<GlfwWindow>();
    if (window.handle != nullptr) {
        glfwDestroyWindow(window.handle);
        window.handle = nullptr;
    }
    glfwTerminate();
}

} // namespace ets
