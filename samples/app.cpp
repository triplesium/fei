#include "app/app.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

#include <print>

using namespace fei;

int main() {
    App()
        .add_plugin<WindowPlugin>()
        .add_plugin<InputPlugin>()
        .add_system(
            StartUp,
            []() {
                std::println("Hello, World!");
            }
        )
        .add_system(
            Update,
            [](Res<KeyInput> key_input, Res<AppStates> app_states) {
                if (key_input->just_pressed(KeyCode::Q)) {
                    std::println("Goodbye, World!");
                    app_states->should_stop = true;
                }
            }
        )
        .run();

    return 0;
}
