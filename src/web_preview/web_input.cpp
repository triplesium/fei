#include "web_preview/web_input.hpp"

#include <algorithm>

namespace fei {

bool WebPreviewInput::set_key(KeyCode key_code, bool pressed) {
    if (!is_supported_key(key_code)) {
        return false;
    }

    std::scoped_lock lock(m_mutex);
    auto iter = std::ranges::find(m_pressed_keys, key_code);
    if (pressed) {
        if (iter == m_pressed_keys.end()) {
            m_pressed_keys.push_back(key_code);
        }
    } else if (iter != m_pressed_keys.end()) {
        m_pressed_keys.erase(iter);
    }
    return true;
}

void WebPreviewInput::clear() {
    std::scoped_lock lock(m_mutex);
    m_pressed_keys.clear();
}

std::vector<KeyCode> WebPreviewInput::pressed_keys() const {
    std::scoped_lock lock(m_mutex);
    return m_pressed_keys;
}

bool WebPreviewInput::is_supported_key(KeyCode key_code) {
    return key_code != KeyCode::Unknown &&
           std::ranges::find(c_key_codes, key_code) !=
               std::ranges::end(c_key_codes);
}

} // namespace fei
