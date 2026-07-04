#pragma once

#include "window/input.hpp"

#include <mutex>
#include <vector>

namespace fei {

class WebPreviewInput {
  public:
    bool set_key(KeyCode key_code, bool pressed);
    void clear();
    std::vector<KeyCode> pressed_keys() const;

  private:
    static bool is_supported_key(KeyCode key_code);

    mutable std::mutex m_mutex;
    std::vector<KeyCode> m_pressed_keys;
};

} // namespace fei
