#pragma once

#include "editor/activity.hpp"
#include "editor/plugin.hpp"
#include "scene/document.hpp"

namespace fei {
class World;
}

namespace fei::editor {

struct HierarchyPanelContext {
    World& world;
    Selection& selection;
    ActivityLog& activity;
    SceneEntityBindings& bindings;
};

class HierarchyPanel {
  public:
    [[nodiscard]] bool is_open() const noexcept { return m_open; }
    void set_open(bool open) noexcept { m_open = open; }

    void draw(HierarchyPanelContext context);

  private:
    bool m_open {true};
};

} // namespace fei::editor
