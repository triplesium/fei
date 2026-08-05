#pragma once

#include "base/result.hpp"

#include <string>

namespace fei {

class App;

namespace editor {

class EditorProjectSession {
  public:
    Status<std::string> open(App& app, bool create_welcome_scene);

    [[nodiscard]] bool is_open() const noexcept { return m_open; }

  private:
    bool m_open {false};
};

} // namespace editor
} // namespace fei
