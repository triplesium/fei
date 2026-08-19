#include "window/input.hpp"

#include <GLFW/glfw3.h>
#include <unordered_map>

namespace fei {

namespace {

std::unordered_map<GLFWwindow*, Vector2> g_scroll_deltas;
std::unordered_map<GLFWwindow*, std::vector<char32_t>> g_characters;

void on_scroll(GLFWwindow* window, double x, double y) {
    g_scroll_deltas[window] += {
        static_cast<float>(x),
        static_cast<float>(y),
    };
}

void on_character(GLFWwindow* window, unsigned int codepoint) {
    g_characters[window].push_back(static_cast<char32_t>(codepoint));
}

} // namespace

void key_input_system(ResRO<Window> win, ResRW<KeyInput> input) {
    auto glfw_window = win->glfw_window;
    input->clear();
    for (KeyCode key_code : c_key_codes) {
        int state = glfwGetKey(glfw_window, static_cast<int>(key_code));
        if (state == GLFW_PRESS) {
            input->press(key_code);
        } else if (state == GLFW_RELEASE) {
            input->release(key_code);
        }
    }
}

void mouse_input_system(ResRO<Window> win, ResRW<MouseInput> input) {
    auto glfw_window = win->glfw_window;
    input->clear();
    for (MouseButton button :
         {MouseButton::Left, MouseButton::Right, MouseButton::Middle}) {
        int state = glfwGetMouseButton(glfw_window, static_cast<int>(button));
        if (state == GLFW_PRESS) {
            input->press(button);
        } else if (state == GLFW_RELEASE) {
            input->release(button);
        }
    }
    double xpos, ypos;
    glfwGetCursorPos(glfw_window, &xpos, &ypos);
    int logical_width = 0;
    int logical_height = 0;
    glfwGetWindowSize(glfw_window, &logical_width, &logical_height);
    if (logical_width > 0 && logical_height > 0) {
        xpos *= static_cast<double>(win->width) / logical_width;
        ypos *= static_cast<double>(win->height) / logical_height;
    }
    input->set_position({static_cast<float>(xpos), static_cast<float>(ypos)});
}

void mouse_scroll_input_system(
    ResRO<Window> win,
    ResRW<MouseScrollInput> input
) {
    input->clear();
    if (win->glfw_window == nullptr) {
        return;
    }
    glfwSetScrollCallback(win->glfw_window, on_scroll);
    const auto item = g_scroll_deltas.find(win->glfw_window);
    if (item != g_scroll_deltas.end()) {
        input->set_delta(item->second);
        item->second = Vector2::Zero;
    }
}

void character_input_system(ResRO<Window> win, ResRW<CharacterInput> input) {
    input->clear();
    if (win->glfw_window == nullptr) {
        return;
    }
    glfwSetCharCallback(win->glfw_window, on_character);
    const auto item = g_characters.find(win->glfw_window);
    if (item != g_characters.end()) {
        for (const auto character : item->second) {
            input->push(character);
        }
        item->second.clear();
    }
}

void apply_virtual_key_input(
    ResRO<VirtualInput> virtual_input,
    ResRW<KeyInput> input
) {
    if (!virtual_input->exclusive()) {
        return;
    }
    for (auto key : c_key_codes) {
        if (virtual_input->pressed(key)) {
            input->press(key);
        } else {
            input->release(key);
        }
    }
}

void apply_virtual_mouse_input(
    ResRO<VirtualInput> virtual_input,
    ResRW<MouseInput> input
) {
    if (!virtual_input->exclusive()) {
        return;
    }
    if (virtual_input->has_mouse_position()) {
        input->set_position(virtual_input->mouse_position());
    }
    for (auto button : c_mouse_buttons) {
        if (virtual_input->pressed(button)) {
            input->press(button);
        } else {
            input->release(button);
        }
    }
}

} // namespace fei
