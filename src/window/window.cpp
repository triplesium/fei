#include "window/window.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace fei {

GLFWwindow* setup_glfw(int width, int height, const std::string& title) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win =
        glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!win) {
        throw std::runtime_error("Failed to create window");
    }

    glfwMakeContextCurrent(win);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    return win;
}

void window_prepare(Res<Window> win_res) {
    glfwPollEvents();
    Window& win = *win_res;
    glfwGetFramebufferSize(win.glfw_window, &win.width, &win.height);
}

void swap_buffers(Res<Window> win_res) {
    glfwSwapBuffers(win_res->glfw_window);
}

void update_should_close(Res<Window> win_res, Res<AppStates> app_states) {
    if (glfwWindowShouldClose(win_res->glfw_window)) {
        app_states->should_stop = true;
    }
}

} // namespace fei
