#include "editor/hierarchy_panel.hpp"

#include "base/optional.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/world.hpp"

#include <algorithm>
#include <imgui.h>
#include <string>
#include <vector>

namespace fei::editor {

namespace {

std::vector<Entity> collect_entities(const World& world) {
    std::vector<Entity> entities;
    for (const auto& [archetype_id, archetype] : world.archetypes()) {
        (void)archetype_id;
        entities.insert(
            entities.end(),
            archetype.entities().begin(),
            archetype.entities().end()
        );
    }
    std::ranges::sort(entities);
    return entities;
}

void draw_entity_node(
    const World& world,
    Entity entity,
    Selection& selection,
    Optional<Entity>& pending_delete
) {
    const bool has_children = world.has_component<Children>(entity) &&
                              !world.get_component<Children>(entity).empty();
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (selection.entity && *selection.entity == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushID(static_cast<int>(entity));
    const bool open = ImGui::TreeNodeEx("entity", flags, "Entity %u", entity);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selection.entity = entity;
        selection.asset = nullopt;
    }
    if (ImGui::BeginPopupContextItem("entity_context")) {
        if (ImGui::MenuItem("Delete")) {
            pending_delete = entity;
        }
        ImGui::EndPopup();
    }

    if (has_children && open) {
        for (const auto child : world.get_component<Children>(entity)) {
            if (world.has_entity(child)) {
                draw_entity_node(world, child, selection, pending_delete);
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void HierarchyPanel::draw(HierarchyPanelContext context) {
    if (!m_open) {
        return;
    }
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Entity")) {
        const auto entity = context.world.entity();
        context.bindings.ensure(entity);
        context.selection.entity = entity;
        context.selection.asset = nullopt;
        context.activity.record(
            OperationSource::User,
            "CreateEntity",
            "Entity " + std::to_string(entity)
        );
    }
    ImGui::Separator();

    auto entities = collect_entities(context.world);
    Optional<Entity> pending_delete;
    for (const auto entity : entities) {
        if (!context.world.parent(entity)) {
            draw_entity_node(
                context.world,
                entity,
                context.selection,
                pending_delete
            );
        }
    }

    if (pending_delete && context.world.has_entity(*pending_delete)) {
        const auto deleted = *pending_delete;
        context.world.despawn(deleted);
        if (context.selection.entity && *context.selection.entity == deleted) {
            context.selection.entity = nullopt;
        }
        context.activity.record(
            OperationSource::User,
            "DeleteEntity",
            "Entity " + std::to_string(deleted)
        );
    }
    ImGui::End();
}

} // namespace fei::editor
