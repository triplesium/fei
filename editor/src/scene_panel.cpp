#include "editor/scene_panel.hpp"

#include <algorithm>
#include <imgui.h>

namespace fei::editor {

void ScenePanel::draw(
    SceneViewport& viewport,
    bool external_change_pending,
    std::string_view error
) {
    viewport.visible = m_open;
    if (!m_open) {
        return;
    }

    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    if (external_change_pending) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.7f, 0.2f, 1.0f},
            "Scene changed on disk; use File > Reload Scene from Disk or Keep "
            "Local Scene."
        );
    }
    if (!error.empty()) {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f}
        );
        ImGui::TextUnformatted(error.data(), error.data() + error.size());
        ImGui::PopStyleColor();
    }

    const auto available = ImGui::GetContentRegionAvail();
    viewport.width = static_cast<uint32>(std::max(available.x, 1.0f));
    viewport.height = static_cast<uint32>(std::max(available.y, 1.0f));

    if (!viewport.texture) {
        ImGui::TextDisabled("Waiting for the 2D render target...");
    } else {
        ImGui::Image(
            viewport.texture.texture_id(),
            available,
            ImVec2 {0.0f, 1.0f},
            ImVec2 {1.0f, 0.0f}
        );
    }
    ImGui::End();
}

} // namespace fei::editor
