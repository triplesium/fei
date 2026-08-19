#pragma once

#include "math/vector.hpp"
#include "refl/reflect.hpp"
#include "window/window.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fei {

struct InputSystems {
    struct Update : SystemSet<Update> {};
    struct ApplyVirtual : SystemSet<ApplyVirtual> {};
    struct ApplyDevtools : SystemSet<ApplyDevtools> {};
};
FEI_REFLECT()
enum class KeyCode : std::int32_t {
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

constexpr KeyCode key_code_from_string(std::string_view name) noexcept {
    for (auto key : c_key_codes) {
        if (name == key_code_to_string(key)) {
            return key;
        }
    }
    return KeyCode::Unknown;
}

enum class KeyState : int32_t {
    Pressed,
    Released,
};

struct KeyEvent {
    KeyCode key_code;
    KeyState state;
};

FEI_REFLECT()
class KeyInput {
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

FEI_REFLECT()
enum class MouseButton : int32_t {
    Left = 0,
    Right = 1,
    Middle = 2,
};

constexpr MouseButton c_mouse_buttons[] = {
    MouseButton::Left,
    MouseButton::Right,
    MouseButton::Middle,
};

class VirtualInput {
  public:
    void set_exclusive(bool exclusive) { m_exclusive = exclusive; }
    [[nodiscard]] bool exclusive() const { return m_exclusive; }

    void set_pressed_keys(std::span<const KeyCode> keys) {
        m_pressed_keys.clear();
        for (auto key : keys) {
            if (key != KeyCode::Unknown) {
                m_pressed_keys.insert(key);
            }
        }
    }

    void set_mouse_position(Vector2 position) {
        m_mouse_position = position;
        m_has_mouse_position = true;
    }

    void set_pressed_mouse_buttons(std::span<const MouseButton> buttons) {
        m_pressed_mouse_buttons.clear();
        m_pressed_mouse_buttons.insert(buttons.begin(), buttons.end());
    }

    void clear_keys() { m_pressed_keys.clear(); }
    void clear_mouse_buttons() { m_pressed_mouse_buttons.clear(); }
    void clear() {
        clear_keys();
        clear_mouse_buttons();
        m_has_mouse_position = false;
    }

    [[nodiscard]] bool pressed(KeyCode key) const {
        return m_pressed_keys.contains(key);
    }

    [[nodiscard]] bool pressed(MouseButton button) const {
        return m_pressed_mouse_buttons.contains(button);
    }

    [[nodiscard]] bool has_mouse_position() const {
        return m_has_mouse_position;
    }

    [[nodiscard]] Vector2 mouse_position() const { return m_mouse_position; }

  private:
    std::unordered_set<KeyCode> m_pressed_keys;
    std::unordered_set<MouseButton> m_pressed_mouse_buttons;
    Vector2 m_mouse_position;
    bool m_has_mouse_position {false};
    bool m_exclusive {false};
};

FEI_REFLECT(Resource)
class MouseInput {
  public:
    MouseInput() {
        for (auto button :
             {MouseButton::Left, MouseButton::Right, MouseButton::Middle}) {
            m_keys[button] = {};
        }
    }

    // Position is in framebuffer/UI viewport pixels with a top-left origin.
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

FEI_REFLECT(Resource)
class MouseScrollInput {
  public:
    void set_delta(Vector2 delta) { m_delta = delta; }
    void scroll(Vector2 delta) { m_delta += delta; }
    void clear() { m_delta = Vector2::Zero; }
    [[nodiscard]] Vector2 delta() const { return m_delta; }

  private:
    Vector2 m_delta;
};

class CharacterInput {
  public:
    void push(char32_t character) { m_characters.push_back(character); }
    void clear() { m_characters.clear(); }
    [[nodiscard]] std::span<const char32_t> characters() const {
        return m_characters;
    }

  private:
    std::vector<char32_t> m_characters;
};

void key_input_system(ResRO<Window> win, ResRW<KeyInput> input);
void mouse_input_system(ResRO<Window> win, ResRW<MouseInput> input);
void mouse_scroll_input_system(
    ResRO<Window> win,
    ResRW<MouseScrollInput> input
);
void character_input_system(ResRO<Window> win, ResRW<CharacterInput> input);
void apply_virtual_key_input(
    ResRO<VirtualInput> virtual_input,
    ResRW<KeyInput> input
);
void apply_virtual_mouse_input(
    ResRO<VirtualInput> virtual_input,
    ResRW<MouseInput> input
);

FEI_REFLECT(Plugin)
class InputPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_resource<KeyInput>();
        app.add_resource<MouseInput>();
        app.add_resource<MouseScrollInput>();
        app.add_resource<CharacterInput>();
        app.add_resource<VirtualInput>();
        app.configure_sets(
            PreUpdate,
            chain(InputSystems::Update {}, InputSystems::ApplyVirtual {})
        );
        app.add_systems(
            PreUpdate,
            key_input_system | in_set<InputSystems::Update>(),
            mouse_input_system | in_set<InputSystems::Update>(),
            mouse_scroll_input_system | in_set<InputSystems::Update>(),
            character_input_system | in_set<InputSystems::Update>(),
            apply_virtual_key_input | in_set<InputSystems::ApplyVirtual>(),
            apply_virtual_mouse_input | in_set<InputSystems::ApplyVirtual>()
        );
    }
};

} // namespace fei
