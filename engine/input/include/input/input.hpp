#pragma once

#include "app/plugin.hpp"
#include "ecs/event.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_set.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fei {

struct InputSystems {
    struct Collect : SystemSet<Collect> {};
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

enum class KeyState : std::int32_t {
    Pressed,
    Released,
};

struct KeyEvent {
    KeyCode key_code {KeyCode::Unknown};
    KeyState state {KeyState::Released};
    bool repeat {false};
};

FEI_REFLECT()
class KeyInput {
  public:
    KeyInput();

    [[nodiscard]] bool pressed(KeyCode key) const;
    [[nodiscard]] bool just_pressed(KeyCode key) const;
    [[nodiscard]] bool just_released(KeyCode key) const;
    void press(KeyCode key);
    void release(KeyCode key);
    void release_all();
    void advance_frame();
    void clear();

  private:
    struct KeyStateInternal {
        bool pressed {false};
        bool just_pressed {false};
        bool just_released {false};
        bool pressed_before_clear {false};
    };
    std::unordered_map<KeyCode, KeyStateInternal> m_keys;
};

FEI_REFLECT()
enum class MouseButton : std::int32_t {
    Left = 0,
    Right = 1,
    Middle = 2,
};

constexpr MouseButton c_mouse_buttons[] = {
    MouseButton::Left,
    MouseButton::Right,
    MouseButton::Middle,
};

struct MouseButtonEvent {
    MouseButton button {MouseButton::Left};
    KeyState state {KeyState::Released};
};

struct MouseMoveEvent {
    Vector2 position;
};

struct MouseScrollEvent {
    Vector2 delta;
};

struct CharacterEvent {
    char32_t character {};
};

struct InputFocusLost {};

class VirtualInput {
  public:
    void set_exclusive(bool exclusive) { m_exclusive = exclusive; }
    [[nodiscard]] bool exclusive() const { return m_exclusive; }

    void set_pressed_keys(std::span<const KeyCode> keys);
    void set_mouse_position(Vector2 position);
    void set_pressed_mouse_buttons(std::span<const MouseButton> buttons);
    void clear_keys();
    void clear_mouse_buttons();
    void clear();
    [[nodiscard]] bool pressed(KeyCode key) const;
    [[nodiscard]] bool pressed(MouseButton button) const;
    [[nodiscard]] bool has_mouse_position() const;
    [[nodiscard]] Vector2 mouse_position() const;

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
    MouseInput();

    void set_position(Vector2 position) { m_position = position; }
    [[nodiscard]] Vector2 position() const { return m_position; }
    void press(MouseButton button);
    void release(MouseButton button);
    void release_all();
    [[nodiscard]] bool pressed(MouseButton button) const;
    [[nodiscard]] bool just_pressed(MouseButton button) const;
    [[nodiscard]] bool just_released(MouseButton button) const;
    void advance_frame();
    void clear();

  private:
    Vector2 m_position;
    struct KeyStateInternal {
        bool pressed {false};
        bool just_pressed {false};
        bool just_released {false};
        bool pressed_before_clear {false};
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

void begin_input_frame(
    ResRW<KeyInput> keys,
    ResRW<MouseInput> mouse,
    ResRW<MouseScrollInput> scroll,
    ResRW<CharacterInput> characters
);
void update_input(
    EventReader<KeyEvent> key_events,
    EventReader<MouseButtonEvent> mouse_button_events,
    EventReader<MouseMoveEvent> mouse_move_events,
    EventReader<MouseScrollEvent> mouse_scroll_events,
    EventReader<CharacterEvent> character_events,
    EventReader<InputFocusLost> focus_lost_events,
    ResRW<KeyInput> keys,
    ResRW<MouseInput> mouse,
    ResRW<MouseScrollInput> scroll,
    ResRW<CharacterInput> characters
);
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
    void setup(App& app) override;
};

} // namespace fei
