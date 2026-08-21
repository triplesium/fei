#include "window_glfw/input.hpp"

#include "app/app.hpp"
#include "input/input.hpp"
#include "window/window.hpp"
#include "window_glfw/window.hpp"

#include <GLFW/glfw3.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {
namespace {

struct GlfwInputQueue {
    std::vector<KeyEvent> keys;
    std::vector<MouseButtonEvent> mouse_buttons;
    std::vector<CharacterEvent> characters;
    Vector2 scroll;
    Vector2 cursor;
    bool cursor_changed {false};
    bool focus_lost {false};
};

std::unordered_map<GLFWwindow*, GlfwInputQueue> g_input_queues;

void on_key(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_UNKNOWN) {
        return;
    }
    g_input_queues[window].keys.push_back(
        KeyEvent {
            .key_code = static_cast<KeyCode>(key),
            .state =
                action == GLFW_RELEASE ? KeyState::Released : KeyState::Pressed,
            .repeat = action == GLFW_REPEAT,
        }
    );
}

void on_mouse_button(GLFWwindow* window, int button, int action, int) {
    if (button < GLFW_MOUSE_BUTTON_LEFT || button > GLFW_MOUSE_BUTTON_MIDDLE ||
        action == GLFW_REPEAT) {
        return;
    }
    g_input_queues[window].mouse_buttons.push_back(
        MouseButtonEvent {
            .button = static_cast<MouseButton>(button),
            .state =
                action == GLFW_PRESS ? KeyState::Pressed : KeyState::Released,
        }
    );
}

void on_cursor_position(GLFWwindow* window, double x, double y) {
    auto& queue = g_input_queues[window];
    queue.cursor = {static_cast<float>(x), static_cast<float>(y)};
    queue.cursor_changed = true;
}

void on_scroll(GLFWwindow* window, double x, double y) {
    g_input_queues[window].scroll += {
        static_cast<float>(x),
        static_cast<float>(y),
    };
}

void on_character(GLFWwindow* window, unsigned int codepoint) {
    g_input_queues[window].characters.push_back(
        CharacterEvent {.character = static_cast<char32_t>(codepoint)}
    );
}

void on_focus(GLFWwindow* window, int focused) {
    if (focused == GLFW_FALSE) {
        g_input_queues[window].focus_lost = true;
    }
}

void collect_glfw_input(
    ResRO<Window> window,
    ResRO<GlfwWindow> glfw,
    EventWriter<KeyEvent> key_events,
    EventWriter<MouseButtonEvent> mouse_button_events,
    EventWriter<MouseMoveEvent> mouse_move_events,
    EventWriter<MouseScrollEvent> scroll_events,
    EventWriter<CharacterEvent> character_events,
    EventWriter<InputFocusLost> focus_lost_events
) {
    auto& queue = g_input_queues[glfw->handle];
    for (auto& event : queue.keys) {
        key_events.send(std::move(event));
    }
    for (auto& event : queue.mouse_buttons) {
        mouse_button_events.send(std::move(event));
    }
    for (auto& event : queue.characters) {
        character_events.send(std::move(event));
    }
    queue.keys.clear();
    queue.mouse_buttons.clear();
    queue.characters.clear();

    if (queue.cursor_changed) {
        double logical_width = 0.0;
        double logical_height = 0.0;
        int glfw_width = 0;
        int glfw_height = 0;
        glfwGetWindowSize(glfw->handle, &glfw_width, &glfw_height);
        logical_width = static_cast<double>(glfw_width);
        logical_height = static_cast<double>(glfw_height);
        auto position = queue.cursor;
        if (logical_width > 0.0 && logical_height > 0.0) {
            position.x *= static_cast<float>(window->width / logical_width);
            position.y *= static_cast<float>(window->height / logical_height);
        }
        mouse_move_events.send(MouseMoveEvent {.position = position});
        queue.cursor_changed = false;
    }
    if (queue.scroll != Vector2::Zero) {
        scroll_events.send(MouseScrollEvent {.delta = queue.scroll});
        queue.scroll = Vector2::Zero;
    }
    if (queue.focus_lost) {
        focus_lost_events.send(InputFocusLost {});
        queue.focus_lost = false;
    }
}

void install_glfw_input_callbacks(GLFWwindow* window) {
    g_input_queues.try_emplace(window);
    glfwSetKeyCallback(window, on_key);
    glfwSetMouseButtonCallback(window, on_mouse_button);
    glfwSetCursorPosCallback(window, on_cursor_position);
    glfwSetScrollCallback(window, on_scroll);
    glfwSetCharCallback(window, on_character);
    glfwSetWindowFocusCallback(window, on_focus);
}

void uninstall_glfw_input_callbacks(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }
    glfwSetKeyCallback(window, nullptr);
    glfwSetMouseButtonCallback(window, nullptr);
    glfwSetCursorPosCallback(window, nullptr);
    glfwSetScrollCallback(window, nullptr);
    glfwSetCharCallback(window, nullptr);
    glfwSetWindowFocusCallback(window, nullptr);
    g_input_queues.erase(window);
}

} // namespace

void GlfwInputPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<InputPlugin>().require<GlfwWindowPlugin>();
}

void GlfwInputPlugin::setup(App& app) {
    install_glfw_input_callbacks(app.resource<GlfwWindow>().handle);
    app.add_systems(
        PreUpdate,
        collect_glfw_input | in_set<InputSystems::Collect>()
    );
}

void GlfwInputPlugin::cleanup(App& app) noexcept {
    if (app.has_resource<GlfwWindow>()) {
        uninstall_glfw_input_callbacks(app.resource<GlfwWindow>().handle);
    }
}

} // namespace fei
