#include "input/input.hpp"

#include "app/app.hpp"

namespace ets {

KeyInput::KeyInput() {
    for (auto key : c_key_codes) {
        m_keys[key] = {};
    }
}

bool KeyInput::pressed(KeyCode key) const {
    return m_keys.at(key).pressed;
}

bool KeyInput::just_pressed(KeyCode key) const {
    return m_keys.at(key).just_pressed;
}

bool KeyInput::just_released(KeyCode key) const {
    return m_keys.at(key).just_released;
}

void KeyInput::press(KeyCode key) {
    if (key != KeyCode::Unknown) {
        auto& state = m_keys[key];
        const bool restoring_cleared_press = state.pressed_before_clear;
        state.just_pressed =
            state.just_pressed || (!state.pressed && !restoring_cleared_press);
        if (restoring_cleared_press) {
            state.just_released = false;
        }
        state.pressed_before_clear = false;
        state.pressed = true;
    }
}

void KeyInput::release(KeyCode key) {
    if (key != KeyCode::Unknown) {
        auto& state = m_keys[key];
        state.just_released = state.just_released || state.pressed;
        state.pressed = false;
    }
}

void KeyInput::release_all() {
    for (auto& [key, state] : m_keys) {
        (void)key;
        state.just_released = state.just_released || state.pressed;
        state.pressed = false;
    }
}

void KeyInput::advance_frame() {
    for (auto& [key, state] : m_keys) {
        (void)key;
        state.just_pressed = false;
        state.just_released = false;
        state.pressed_before_clear = false;
    }
}

void KeyInput::clear() {
    for (auto& [key, state] : m_keys) {
        (void)key;
        state.just_pressed = false;
        state.just_released = state.pressed;
        state.pressed_before_clear = state.pressed;
        state.pressed = false;
    }
}

void VirtualInput::set_pressed_keys(std::span<const KeyCode> keys) {
    m_pressed_keys.clear();
    for (auto key : keys) {
        if (key != KeyCode::Unknown) {
            m_pressed_keys.insert(key);
        }
    }
}

void VirtualInput::set_mouse_position(Vector2 position) {
    m_mouse_position = position;
    m_has_mouse_position = true;
}

void VirtualInput::set_pressed_mouse_buttons(
    std::span<const MouseButton> buttons
) {
    m_pressed_mouse_buttons.clear();
    m_pressed_mouse_buttons.insert(buttons.begin(), buttons.end());
}

void VirtualInput::clear_keys() {
    m_pressed_keys.clear();
}

void VirtualInput::clear_mouse_buttons() {
    m_pressed_mouse_buttons.clear();
}

void VirtualInput::clear() {
    clear_keys();
    clear_mouse_buttons();
    m_has_mouse_position = false;
}

bool VirtualInput::pressed(KeyCode key) const {
    return m_pressed_keys.contains(key);
}

bool VirtualInput::pressed(MouseButton button) const {
    return m_pressed_mouse_buttons.contains(button);
}

bool VirtualInput::has_mouse_position() const {
    return m_has_mouse_position;
}

Vector2 VirtualInput::mouse_position() const {
    return m_mouse_position;
}

MouseInput::MouseInput() {
    for (auto button : c_mouse_buttons) {
        m_keys[button] = {};
    }
}

void MouseInput::press(MouseButton button) {
    auto& state = m_keys[button];
    const bool restoring_cleared_press = state.pressed_before_clear;
    state.just_pressed =
        state.just_pressed || (!state.pressed && !restoring_cleared_press);
    if (restoring_cleared_press) {
        state.just_released = false;
    }
    state.pressed_before_clear = false;
    state.pressed = true;
}

void MouseInput::release(MouseButton button) {
    auto& state = m_keys[button];
    state.just_released = state.just_released || state.pressed;
    state.pressed = false;
}

void MouseInput::release_all() {
    for (auto& [button, state] : m_keys) {
        (void)button;
        state.just_released = state.just_released || state.pressed;
        state.pressed = false;
    }
}

bool MouseInput::pressed(MouseButton button) const {
    return m_keys.at(button).pressed;
}

bool MouseInput::just_pressed(MouseButton button) const {
    return m_keys.at(button).just_pressed;
}

bool MouseInput::just_released(MouseButton button) const {
    return m_keys.at(button).just_released;
}

void MouseInput::advance_frame() {
    for (auto& [button, state] : m_keys) {
        (void)button;
        state.just_pressed = false;
        state.just_released = false;
        state.pressed_before_clear = false;
    }
}

void MouseInput::clear() {
    for (auto& [button, state] : m_keys) {
        (void)button;
        state.just_pressed = false;
        state.just_released = state.pressed;
        state.pressed_before_clear = state.pressed;
        state.pressed = false;
    }
}

void begin_input_frame(
    ResRW<KeyInput> keys,
    ResRW<MouseInput> mouse,
    ResRW<MouseScrollInput> scroll,
    ResRW<CharacterInput> characters
) {
    keys->advance_frame();
    mouse->advance_frame();
    scroll->clear();
    characters->clear();
}

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
) {
    while (const auto event = key_events.next()) {
        if (event->state == KeyState::Pressed) {
            keys->press(event->key_code);
        } else {
            keys->release(event->key_code);
        }
    }
    while (const auto event = mouse_button_events.next()) {
        if (event->state == KeyState::Pressed) {
            mouse->press(event->button);
        } else {
            mouse->release(event->button);
        }
    }
    while (const auto event = mouse_move_events.next()) {
        mouse->set_position(event->position);
    }
    while (const auto event = mouse_scroll_events.next()) {
        scroll->scroll(event->delta);
    }
    while (const auto event = character_events.next()) {
        characters->push(event->character);
    }
    bool focus_lost = false;
    while (focus_lost_events.next()) {
        focus_lost = true;
    }
    if (focus_lost) {
        keys->release_all();
        mouse->release_all();
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

void InputPlugin::setup(App& app) {
    app.add_event<KeyEvent>()
        .add_event<MouseButtonEvent>()
        .add_event<MouseMoveEvent>()
        .add_event<MouseScrollEvent>()
        .add_event<CharacterEvent>()
        .add_event<InputFocusLost>()
        .add_resource<KeyInput>()
        .add_resource<MouseInput>()
        .add_resource<MouseScrollInput>()
        .add_resource<CharacterInput>()
        .add_resource<VirtualInput>()
        .configure_sets(
            PreUpdate,
            chain(
                InputSystems::Collect {},
                InputSystems::Update {},
                InputSystems::ApplyVirtual {},
                InputSystems::ApplyDevtools {}
            )
        )
        .add_systems(
            PreUpdate,
            chain(begin_input_frame, update_input) |
                in_set<InputSystems::Update>(),
            apply_virtual_key_input | in_set<InputSystems::ApplyVirtual>(),
            apply_virtual_mouse_input | in_set<InputSystems::ApplyVirtual>()
        );
}

} // namespace ets
