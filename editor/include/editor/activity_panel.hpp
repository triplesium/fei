#pragma once

#include "editor/activity.hpp"

#include <cstddef>

namespace fei::editor {

class ActivityPanel {
  public:
    [[nodiscard]] bool is_open() const noexcept { return m_open; }
    void set_open(bool open) noexcept { m_open = open; }

    void draw(
        const ActivityLog& activity,
        const ExternalAgentStatus& agent,
        std::size_t component_count
    );

  private:
    bool m_open {true};
};

} // namespace fei::editor
