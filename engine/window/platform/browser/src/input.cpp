#include "window_browser/input.hpp"

#include "app/app.hpp"
#include "input/input.hpp"
#include "window_browser/window.hpp"

#include <array>
#include <cstring>
#include <emscripten/html5.h>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {
namespace {

struct BrowserInputQueue {
    std::vector<KeyEvent> keys;
    std::vector<MouseButtonEvent> mouse_buttons;
    std::vector<MouseMoveEvent> mouse_moves;
    std::vector<MouseScrollEvent> scroll;
    std::vector<CharacterEvent> characters;
    bool focus_lost {false};
};

BrowserInputQueue g_input_queue;
std::string g_canvas_selector;

KeyCode browser_key_code(std::string_view code) {
    if (code.size() == 4 && code.starts_with("Key") && code[3] >= 'A' &&
        code[3] <= 'Z') {
        return static_cast<KeyCode>(code[3]);
    }
    if (code.size() == 6 && code.starts_with("Digit") && code[5] >= '0' &&
        code[5] <= '9') {
        return static_cast<KeyCode>(code[5]);
    }
    if (code.size() >= 2 && code[0] == 'F') {
        int number = 0;
        for (std::size_t index = 1; index < code.size(); ++index) {
            if (code[index] < '0' || code[index] > '9') {
                number = 0;
                break;
            }
            number = number * 10 + (code[index] - '0');
        }
        if (number >= 1 && number <= 25) {
            return static_cast<KeyCode>(
                static_cast<int>(KeyCode::F1) + number - 1
            );
        }
    }
    if (code.size() == 7 && code.starts_with("Numpad") && code[6] >= '0' &&
        code[6] <= '9') {
        return static_cast<KeyCode>(
            static_cast<int>(KeyCode::Keypad0) + code[6] - '0'
        );
    }

    static constexpr std::array mappings {
        std::pair {"Space", KeyCode::Space},
        std::pair {"Quote", KeyCode::Apostrophe},
        std::pair {"Comma", KeyCode::Comma},
        std::pair {"Minus", KeyCode::Minus},
        std::pair {"Period", KeyCode::Period},
        std::pair {"Slash", KeyCode::Slash},
        std::pair {"Semicolon", KeyCode::Semicolon},
        std::pair {"Equal", KeyCode::Equal},
        std::pair {"BracketLeft", KeyCode::LeftBracket},
        std::pair {"Backslash", KeyCode::Backslash},
        std::pair {"BracketRight", KeyCode::RightBracket},
        std::pair {"Backquote", KeyCode::GraveAccent},
        std::pair {"Escape", KeyCode::Escape},
        std::pair {"Enter", KeyCode::Enter},
        std::pair {"Tab", KeyCode::Tab},
        std::pair {"Backspace", KeyCode::Backspace},
        std::pair {"Insert", KeyCode::Insert},
        std::pair {"Delete", KeyCode::Delete},
        std::pair {"ArrowRight", KeyCode::Right},
        std::pair {"ArrowLeft", KeyCode::Left},
        std::pair {"ArrowDown", KeyCode::Down},
        std::pair {"ArrowUp", KeyCode::Up},
        std::pair {"PageUp", KeyCode::PageUp},
        std::pair {"PageDown", KeyCode::PageDown},
        std::pair {"Home", KeyCode::Home},
        std::pair {"End", KeyCode::End},
        std::pair {"CapsLock", KeyCode::CapsLock},
        std::pair {"ScrollLock", KeyCode::ScrollLock},
        std::pair {"NumLock", KeyCode::NumLock},
        std::pair {"PrintScreen", KeyCode::PrintScreen},
        std::pair {"Pause", KeyCode::Pause},
        std::pair {"NumpadDecimal", KeyCode::KeypadDecimal},
        std::pair {"NumpadDivide", KeyCode::KeypadDivide},
        std::pair {"NumpadMultiply", KeyCode::KeypadMultiply},
        std::pair {"NumpadSubtract", KeyCode::KeypadSubtract},
        std::pair {"NumpadAdd", KeyCode::KeypadAdd},
        std::pair {"NumpadEnter", KeyCode::KeypadEnter},
        std::pair {"NumpadEqual", KeyCode::KeypadEqual},
        std::pair {"ShiftLeft", KeyCode::LeftShift},
        std::pair {"ControlLeft", KeyCode::LeftControl},
        std::pair {"AltLeft", KeyCode::LeftAlt},
        std::pair {"MetaLeft", KeyCode::LeftSuper},
        std::pair {"ShiftRight", KeyCode::RightShift},
        std::pair {"ControlRight", KeyCode::RightControl},
        std::pair {"AltRight", KeyCode::RightAlt},
        std::pair {"MetaRight", KeyCode::RightSuper},
        std::pair {"ContextMenu", KeyCode::Menu},
    };
    for (const auto& [browser_code, key_code] : mappings) {
        if (code == browser_code) {
            return key_code;
        }
    }
    return KeyCode::Unknown;
}

std::optional<char32_t> single_utf8_character(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    const auto first = static_cast<unsigned char>(text[0]);
    std::size_t length = 0;
    char32_t character = 0;
    if ((first & 0x80U) == 0) {
        length = 1;
        character = first;
    } else if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        character = first & 0x1FU;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        character = first & 0x0FU;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        character = first & 0x07U;
    } else {
        return std::nullopt;
    }
    if (text.size() != length) {
        return std::nullopt;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto continuation = static_cast<unsigned char>(text[index]);
        if ((continuation & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        character = (character << 6U) | (continuation & 0x3FU);
    }
    return character;
}

EM_BOOL
on_browser_key(int event_type, const EmscriptenKeyboardEvent* event, void*) {
    const auto key_code = browser_key_code(event->code);
    if (key_code == KeyCode::Unknown) {
        return EM_FALSE;
    }
    g_input_queue.keys.push_back(
        KeyEvent {
            .key_code = key_code,
            .state = event_type == EMSCRIPTEN_EVENT_KEYUP ? KeyState::Released :
                                                            KeyState::Pressed,
            .repeat = event->repeat,
        }
    );
    return EM_FALSE;
}

EM_BOOL on_browser_keypress(int, const EmscriptenKeyboardEvent* event, void*) {
    const auto character = single_utf8_character(event->key);
    if (character) {
        g_input_queue.characters.push_back(
            CharacterEvent {.character = *character}
        );
    }
    return EM_FALSE;
}

std::optional<MouseButton> browser_mouse_button(unsigned short button) {
    switch (button) {
        case 0:
            return MouseButton::Left;
        case 1:
            return MouseButton::Middle;
        case 2:
            return MouseButton::Right;
        default:
            return std::nullopt;
    }
}

Vector2 browser_mouse_position(const EmscriptenMouseEvent& event) {
    double css_width = 0.0;
    double css_height = 0.0;
    int width = 0;
    int height = 0;
    emscripten_get_element_css_size(
        g_canvas_selector.c_str(),
        &css_width,
        &css_height
    );
    emscripten_get_canvas_element_size(
        g_canvas_selector.c_str(),
        &width,
        &height
    );
    const auto scale_x =
        css_width > 0.0 ? static_cast<double>(width) / css_width : 1.0;
    const auto scale_y =
        css_height > 0.0 ? static_cast<double>(height) / css_height : 1.0;
    return {
        static_cast<float>(event.targetX * scale_x),
        static_cast<float>(event.targetY * scale_y),
    };
}

EM_BOOL
on_browser_mouse(int event_type, const EmscriptenMouseEvent* event, void*) {
    g_input_queue.mouse_moves.push_back(
        MouseMoveEvent {.position = browser_mouse_position(*event)}
    );
    if (event_type != EMSCRIPTEN_EVENT_MOUSEMOVE) {
        const auto button = browser_mouse_button(event->button);
        if (button) {
            g_input_queue.mouse_buttons.push_back(
                MouseButtonEvent {
                    .button = *button,
                    .state = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN ?
                                 KeyState::Pressed :
                                 KeyState::Released,
                }
            );
        }
    }
    return EM_FALSE;
}

EM_BOOL on_browser_wheel(int, const EmscriptenWheelEvent* event, void*) {
    g_input_queue.scroll.push_back(
        MouseScrollEvent {
            .delta = {
                static_cast<float>(-event->deltaX),
                static_cast<float>(-event->deltaY),
            },
        }
    );
    return EM_FALSE;
}

EM_BOOL on_browser_blur(int, const EmscriptenFocusEvent*, void*) {
    g_input_queue.focus_lost = true;
    return EM_FALSE;
}

void collect_browser_input(
    EventWriter<KeyEvent> key_events,
    EventWriter<MouseButtonEvent> mouse_button_events,
    EventWriter<MouseMoveEvent> mouse_move_events,
    EventWriter<MouseScrollEvent> scroll_events,
    EventWriter<CharacterEvent> character_events,
    EventWriter<InputFocusLost> focus_lost_events
) {
    for (auto& event : g_input_queue.keys) {
        key_events.send(std::move(event));
    }
    for (auto& event : g_input_queue.mouse_buttons) {
        mouse_button_events.send(std::move(event));
    }
    for (auto& event : g_input_queue.mouse_moves) {
        mouse_move_events.send(std::move(event));
    }
    for (auto& event : g_input_queue.scroll) {
        scroll_events.send(std::move(event));
    }
    for (auto& event : g_input_queue.characters) {
        character_events.send(std::move(event));
    }
    g_input_queue.keys.clear();
    g_input_queue.mouse_buttons.clear();
    g_input_queue.mouse_moves.clear();
    g_input_queue.scroll.clear();
    g_input_queue.characters.clear();
    if (g_input_queue.focus_lost) {
        focus_lost_events.send(InputFocusLost {});
        g_input_queue.focus_lost = false;
    }
}

void install_browser_input_callbacks(const std::string& selector) {
    g_canvas_selector = selector;
    g_input_queue = {};
    emscripten_set_keydown_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        on_browser_key
    );
    emscripten_set_keyup_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        on_browser_key
    );
    emscripten_set_keypress_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        on_browser_keypress
    );
    emscripten_set_mousedown_callback(
        selector.c_str(),
        nullptr,
        true,
        on_browser_mouse
    );
    emscripten_set_mouseup_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        on_browser_mouse
    );
    emscripten_set_mousemove_callback(
        selector.c_str(),
        nullptr,
        true,
        on_browser_mouse
    );
    emscripten_set_wheel_callback(
        selector.c_str(),
        nullptr,
        true,
        on_browser_wheel
    );
    emscripten_set_blur_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        on_browser_blur
    );
}

void uninstall_browser_input_callbacks(const std::string& selector) {
    emscripten_set_keydown_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        nullptr
    );
    emscripten_set_keyup_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        nullptr
    );
    emscripten_set_keypress_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        nullptr
    );
    emscripten_set_mousedown_callback(selector.c_str(), nullptr, true, nullptr);
    emscripten_set_mouseup_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        nullptr
    );
    emscripten_set_mousemove_callback(selector.c_str(), nullptr, true, nullptr);
    emscripten_set_wheel_callback(selector.c_str(), nullptr, true, nullptr);
    emscripten_set_blur_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW,
        nullptr,
        true,
        nullptr
    );
    g_input_queue = {};
    g_canvas_selector.clear();
}

} // namespace

void BrowserInputPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<BrowserWindowPlugin>().require<InputPlugin>();
}

void BrowserInputPlugin::setup(App& app) {
    install_browser_input_callbacks(app.resource<BrowserCanvas>().selector);
    app.add_systems(
        PreUpdate,
        collect_browser_input | in_set<InputSystems::Collect>()
    );
}

void BrowserInputPlugin::cleanup(App& app) noexcept {
    if (app.has_resource<BrowserCanvas>()) {
        uninstall_browser_input_callbacks(
            app.resource<BrowserCanvas>().selector
        );
    }
}

} // namespace fei
