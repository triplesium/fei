#pragma once

#include "asset/path.hpp"
#include "editor/activity.hpp"
#include "editor/component_operations.hpp"
#include "editor/plugin.hpp"

#include <functional>

namespace fei {
class World;
}

namespace fei::editor {

struct InspectorPanelContext {
    World& world;
    Selection& selection;
    const ComponentOperations& operations;
    ActivityLog& activity;
    std::function<void(const AssetPath&)> draw_asset;
};

class InspectorPanel {
  public:
    [[nodiscard]] bool is_open() const noexcept { return m_open; }
    void set_open(bool open) noexcept { m_open = open; }

    void draw(InspectorPanelContext context);

  private:
    bool m_open {true};
};

} // namespace fei::editor
