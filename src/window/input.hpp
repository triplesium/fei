#pragma once

#include "math/vector.hpp"
#include "window/window.hpp"

#include <cstdint>
#include <unordered_map>

namespace fei {
enum class KeyCode : std::int32_t {
#define KEY_CODE(name, code) name = code,
#include "keycode.def"
#undef KEY_CODE
};

constexpr KeyCode c_key_codes[] = {
#define KEY_CODE(name, code) KeyCode::name,
#include "keycode.def"
#undef KEY_CODE
};

constexpr const char* key_code_to_string(KeyCode keyCode) noexcept {
    switch (keyCode) {
#define KEY_CODE(name, code) \
    case KeyCode::name:      \
        return #name;
#include "keycode.def"
#undef KEY_CODE
    }
    return "Unknown";
}

enum class KeyState : int32_t {
    Pressed,
    Released,
};

struct KeyEvent {
    KeyCode key_code;
    KeyState state;
};

class KeyInput {
  public:
    bool pressed(KeyCode key) const { return keys.at(key).down_this_frame; }
    bool just_pressed(KeyCode key) const {
        return keys.at(key).down_this_frame && !keys.at(key).down_last_frame;
    }
    bool just_released(KeyCode key) const {
        return !keys.at(key).down_this_frame && keys.at(key).down_last_frame;
    }

    void press(KeyCode key) { keys[key].down_this_frame = true; }
    void release(KeyCode key) { keys[key].down_this_frame = false; }
    void clear() {
        for (auto& [key, state] : keys) {
            state.down_last_frame = state.down_this_frame;
            state.down_this_frame = false;
        }
    }

  private:
    struct KeyStateInternal {
        bool down_this_frame {false};
        bool down_last_frame {false};
    };
    std::unordered_map<KeyCode, KeyStateInternal> keys;
};

enum class MouseButton : int32_t {
    Left = 0,
    Right = 1,
    Middle = 2,
};

class MouseInput {
  public:
    void set_position(Vector2 position) { m_position = position; }
    Vector2 position() const { return m_position; }

    void press(MouseButton button) { keys[button].down_this_frame = true; }
    void release(MouseButton button) { keys[button].down_this_frame = false; }
    bool pressed(MouseButton button) const {
        return keys.at(button).down_this_frame;
    }
    bool just_pressed(MouseButton button) const {
        return keys.at(button).down_this_frame &&
               !keys.at(button).down_last_frame;
    }
    bool just_released(MouseButton button) const {
        return !keys.at(button).down_this_frame &&
               keys.at(button).down_last_frame;
    }
    void clear() {
        for (auto& [key, state] : keys) {
            state.down_last_frame = state.down_this_frame;
            state.down_this_frame = false;
        }
    }

  private:
    Vector2 m_position;
    struct KeyStateInternal {
        bool down_this_frame {false};
        bool down_last_frame {false};
    };
    std::unordered_map<MouseButton, KeyStateInternal> keys;
};

void key_input_system(Res<Window> win, Res<KeyInput> input);
void mouse_input_system(Res<Window> win, Res<MouseInput> input);

class InputPlugin : public Plugin {
  public:
    void setup(App& app) {
        app.add_resource<KeyInput>();
        app.add_resource<MouseInput>();
        app.add_systems(PreUpdate, key_input_system, mouse_input_system);
    }
};

} // namespace fei
