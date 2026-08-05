#include "editor/activity_panel.hpp"

#include <imgui.h>

namespace fei::editor {

namespace {

const char* operation_source_name(OperationSource source) {
    switch (source) {
        case OperationSource::Editor:
            return "Editor";
        case OperationSource::User:
            return "User";
        case OperationSource::ExternalAgent:
            return "External agent";
    }
    return "Unknown";
}

} // namespace

void ActivityPanel::draw(
    const ActivityLog& activity,
    const ExternalAgentStatus& agent,
    std::size_t component_count
) {
    if (!m_open) {
        return;
    }
    if (!ImGui::Begin("Agent Activity")) {
        ImGui::End();
        return;
    }

    const bool connected =
        agent.connection == ExternalAgentConnection::Connected;
    ImGui::TextColored(
        connected ? ImVec4 {0.35f, 0.85f, 0.45f, 1.0f} :
                    ImVec4 {0.75f, 0.75f, 0.75f, 1.0f},
        "%s",
        connected ? "External agent connected" : "External agent disconnected"
    );
    ImGui::SameLine();
    ImGui::TextDisabled("| generic component API: %zu types", component_count);
    ImGui::Separator();

    if (ImGui::BeginTable(
            "activity",
            5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable
        )) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn(
            "Source",
            ImGuiTableColumnFlags_WidthFixed,
            105.0f
        );
        ImGui::TableSetupColumn(
            "Action",
            ImGuiTableColumnFlags_WidthFixed,
            150.0f
        );
        ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Status",
            ImGuiTableColumnFlags_WidthFixed,
            64.0f
        );
        ImGui::TableHeadersRow();

        for (const auto& entry : activity.entries()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text(
                "%llu",
                static_cast<unsigned long long>(entry.sequence)
            );
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(operation_source_name(entry.source));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(entry.action.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(entry.detail.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextColored(
                entry.succeeded ? ImVec4 {0.35f, 0.85f, 0.45f, 1.0f} :
                                  ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
                "%s",
                entry.succeeded ? "OK" : "Failed"
            );
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace fei::editor
