#pragma once

#include "base/types.hpp"
#include "imgui/texture.hpp"

#include <string_view>

namespace fei::editor {

struct SceneViewport {
    ImGuiTextureHandle texture;
    uint32 width {1280};
    uint32 height {720};
    bool visible {true};
};

class ScenePanel {
  public:
    [[nodiscard]] bool is_open() const noexcept { return m_open; }
    void set_open(bool open) noexcept { m_open = open; }

    void draw(
        SceneViewport& viewport,
        bool external_change_pending,
        std::string_view error
    );

  private:
    bool m_open {true};
};

} // namespace fei::editor
