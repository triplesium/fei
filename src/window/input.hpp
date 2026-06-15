#pragma once

#include "math/vector.hpp"
#include "refl/reflect.hpp"
#include "window/window.hpp"

#include <cstdint>
#include <unordered_map>

namespace fei {
enum class FEI_REFLECT KeyCode : std::int32_t {
#define KEY_CODE(name, code) name = (code),
#include "keycode.def"
#undef KEY_CODE
};

constexpr KeyCode c_key_codes[] = {
#define KEY_CODE(name, code) KeyCode::name,
#include "keycode.def"
#undef KEY_CODE
};

constexpr const char* key_code_to_string(KeyCode key_code) noexcept {
    switch (key_code) {
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

class FEI_REFLECT KeyInput {
  public:
    KeyInput() {
        for (auto key : c_key_codes) {
            m_keys[key] = {};
        }
    }

    bool pressed(KeyCode key) const { return m_keys.at(key).down_this_frame; }
    bool just_pressed(KeyCode key) const {
        return m_keys.at(key).down_this_frame &&
               !m_keys.at(key).down_last_frame;
    }
    bool just_released(KeyCode key) const {
        return !m_keys.at(key).down_this_frame &&
               m_keys.at(key).down_last_frame;
    }

    void press(KeyCode key) { m_keys[key].down_this_frame = true; }
    void release(KeyCode key) { m_keys[key].down_this_frame = false; }
    void clear() {
        for (auto& [key, state] : m_keys) {
            state.down_last_frame = state.down_this_frame;
            state.down_this_frame = false;
        }
    }

  private:
    struct KeyStateInternal {
        bool down_this_frame {false};
        bool down_last_frame {false};
    };
    std::unordered_map<KeyCode, KeyStateInternal> m_keys;
};

enum class MouseButton : int32_t {
    Left = 0,
    Right = 1,
    Middle = 2,
};

class MouseInput {
  public:
    MouseInput() {
        for (auto button :
             {MouseButton::Left, MouseButton::Right, MouseButton::Middle}) {
            m_keys[button] = {};
        }
    }

    void set_position(Vector2 position) { m_position = position; }
    Vector2 position() const { return m_position; }

    void press(MouseButton button) { m_keys[button].down_this_frame = true; }
    void release(MouseButton button) { m_keys[button].down_this_frame = false; }
    bool pressed(MouseButton button) const {
        return m_keys.at(button).down_this_frame;
    }
    bool just_pressed(MouseButton button) const {
        return m_keys.at(button).down_this_frame &&
               !m_keys.at(button).down_last_frame;
    }
    bool just_released(MouseButton button) const {
        return !m_keys.at(button).down_this_frame &&
               m_keys.at(button).down_last_frame;
    }
    void clear() {
        for (auto& [key, state] : m_keys) {
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
    std::unordered_map<MouseButton, KeyStateInternal> m_keys;
};

void key_input_system(Res<Window> win, Res<KeyInput> input);
void mouse_input_system(Res<Window> win, Res<MouseInput> input);

class InputPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_resource<KeyInput>();
        app.add_resource<MouseInput>();
        app.add_systems(PreUpdate, key_input_system, mouse_input_system);
    }
};

} // namespace fei
