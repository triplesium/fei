#include "window/input.hpp"

#include <glfw/glfw3.h>

namespace fei {
void key_input_system(Res<Window> win, Res<KeyInput> input) {
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

void mouse_input_system(Res<Window> win, Res<MouseInput> input) {
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
    input->set_position({(float)xpos, (float)ypos});
}

} // namespace fei
