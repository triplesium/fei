#include "editor/plugin.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/serialization.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "editor/activity.hpp"
#include "editor/component_registry.hpp"
#include "imgui/plugin.hpp"
#include "imgui/renderer.hpp"
#include "refl/cls.hpp"
#include "refl/property.hpp"
#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "rendering/plugin.hpp"
#include "sprite/components.hpp"
#include "sprite/output.hpp"
#include "sprite/plugin.hpp"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fei::editor {

namespace {

struct EditorUiState {
    ImTextureID scene_texture_id {ImTextureID_Invalid};
    std::shared_ptr<const Texture> registered_scene_texture;
    bool layout_initialized {false};
    bool style_initialized {false};
    bool show_scene {true};
    bool show_hierarchy {true};
    bool show_inspector {true};
    bool show_activity {true};
};

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

std::string reflected_type_name(TypeId type) {
    auto reflected_type = Registry::instance().try_get_type(type);
    return reflected_type ? reflected_type->stripped_name() :
                            "Type " + std::to_string(type.id());
}

void initialize_style(EditorUiState& state) {
    if (state.style_initialized) {
        return;
    }
    state.style_initialized = true;

    auto& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
}

void build_default_layout(ImGuiID dockspace, EditorUiState& state) {
    if (state.layout_initialized) {
        return;
    }
    state.layout_initialized = true;

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(
        dockspace,
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace)
    );
    ImGui::DockBuilderSetNodePos(dockspace, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

    ImGuiID center = dockspace;
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGuiID bottom = 0;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, &bottom, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Agent Activity", bottom);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderFinish(dockspace);
}

void draw_main_menu(World& world, EditorUiState& state) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Exit")) {
            world.resource<AppStates>().should_stop = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Scene", nullptr, &state.show_scene);
        ImGui::MenuItem("Hierarchy", nullptr, &state.show_hierarchy);
        ImGui::MenuItem("Inspector", nullptr, &state.show_inspector);
        ImGui::MenuItem("Agent Activity", nullptr, &state.show_activity);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Agent")) {
        const auto& status =
            static_cast<const World&>(world).resource<ExternalAgentStatus>();
        const bool connected =
            status.connection == ExternalAgentConnection::Connected;
        ImGui::TextDisabled(
            connected ? "External agent connected" :
                        "External agent disconnected"
        );
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

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

void draw_hierarchy(World& world, Selection& selection, ActivityLog& activity) {
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Entity")) {
        const auto entity = world.entity();
        selection.entity = entity;
        activity.record(
            OperationSource::User,
            "CreateEntity",
            "Entity " + std::to_string(entity)
        );
    }
    ImGui::Separator();

    auto entities = collect_entities(world);
    Optional<Entity> pending_delete;
    for (const auto entity : entities) {
        if (!world.parent(entity)) {
            draw_entity_node(world, entity, selection, pending_delete);
        }
    }

    if (pending_delete && world.has_entity(*pending_delete)) {
        const auto deleted = *pending_delete;
        world.despawn(deleted);
        if (selection.entity && *selection.entity == deleted) {
            selection.entity = nullopt;
        }
        activity.record(
            OperationSource::User,
            "DeleteEntity",
            "Entity " + std::to_string(deleted)
        );
    }
    ImGui::End();
}

void draw_field_label(std::string_view label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(130.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

bool draw_reflected_value(
    Ref value,
    std::string_view label,
    int depth,
    const ComponentRegistry& components
);

bool draw_reflected_object(
    Ref value,
    int depth,
    const ComponentRegistry& components
) {
    if (depth > 8) {
        ImGui::TextDisabled("Reflection depth limit reached");
        return false;
    }

    auto cls = Registry::instance().try_get_cls(value.type_id());
    if (!cls) {
        return false;
    }

    auto properties = cls->get_properties();
    std::ranges::sort(properties, {}, [](const Property* property) {
        return property->name();
    });

    bool changed = false;
    for (const auto* property : properties) {
        auto property_value = property->get(value);
        if (!property_value) {
            ImGui::TextDisabled("%s: unavailable", property->name().c_str());
            continue;
        }
        changed |= draw_reflected_value(
            *property_value,
            property->name(),
            depth + 1,
            components
        );
    }
    return changed;
}

bool draw_reflected_value(
    Ref value,
    std::string_view label,
    int depth,
    const ComponentRegistry& components
) {
    const std::string label_string(label);
    ImGui::PushID(label_string.c_str());

    bool changed = false;
    if (auto* data = value.try_get<bool>()) {
        draw_field_label(label);
        changed = ImGui::Checkbox("##value", data);
    } else if (auto* data = value.try_get<float>()) {
        draw_field_label(label);
        changed = ImGui::DragFloat("##value", data, 0.05f);
    } else if (auto* data = value.try_get<double>()) {
        draw_field_label(label);
        changed =
            ImGui::DragScalar("##value", ImGuiDataType_Double, data, 0.05f);
    } else if (auto* data = value.try_get<int>()) {
        draw_field_label(label);
        changed = ImGui::DragInt("##value", data, 0.1f);
    } else if (auto* data = value.try_get<unsigned int>()) {
        draw_field_label(label);
        changed = ImGui::DragScalar("##value", ImGuiDataType_U32, data, 0.1f);
    } else if (auto preview = components.preview(value)) {
        draw_field_label(label);
        ImGui::TextUnformatted(preview->c_str());
    } else if (Registry::instance().try_get_cls(value.type_id())) {
        const auto flags =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool open = ImGui::TreeNodeEx(label_string.c_str(), flags);
        if (open) {
            changed = draw_reflected_object(value, depth, components);
            ImGui::TreePop();
        }
    } else {
        draw_field_label(label);
        ImGui::TextDisabled("%s", reflected_type_name(value.type_id()).c_str());
    }

    ImGui::PopID();
    return changed;
}

void record_component_result(
    ActivityLog& activity,
    std::string action,
    Entity entity,
    const ComponentInfo& component,
    const Status<ComponentError>& result
) {
    const auto detail = "Entity " + std::to_string(entity) + " / " +
                        component.name +
                        (result ? "" : ": " + result.error().message);
    activity.record(
        OperationSource::User,
        std::move(action),
        detail,
        result.has_value()
    );
}

void draw_inspector(
    World& world,
    Selection& selection,
    const ComponentRegistry& components,
    ActivityLog& activity
) {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    if (!selection.entity || !world.has_entity(*selection.entity)) {
        selection.entity = nullopt;
        ImGui::TextDisabled("Select an entity to inspect it.");
        ImGui::End();
        return;
    }

    const auto entity = *selection.entity;
    ImGui::Text("Entity %u", entity);
    ImGui::Separator();

    Optional<TypeId> pending_remove;
    for (const auto& component : components.entries()) {
        if (!world.has_component(entity, component.type)) {
            continue;
        }

        ImGui::PushID(static_cast<int>(component.type.id()));
        const bool open = ImGui::CollapsingHeader(
            component.name.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen
        );
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
        if (ImGui::SmallButton("x")) {
            pending_remove = component.type;
        }
        if (open) {
            auto value = world.get_component(entity, component.type);
            if (draw_reflected_object(value, 0, components)) {
                world.mark_component_changed(entity, component.type);
                activity.record(
                    OperationSource::User,
                    "SetComponentProperty",
                    "Entity " + std::to_string(entity) + " / " + component.name
                );
            }
            ImGui::Spacing();
        }
        ImGui::PopID();
    }

    if (pending_remove) {
        const auto* component = components.find(*pending_remove);
        const auto result = components.remove(world, entity, *pending_remove);
        if (component) {
            record_component_result(
                activity,
                "RemoveComponent",
                entity,
                *component,
                result
            );
        }
    }

    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("add_component");
    }
    if (ImGui::BeginPopup("add_component")) {
        for (const auto& component : components.entries()) {
            if (world.has_component(entity, component.type)) {
                continue;
            }
            if (ImGui::MenuItem(component.name.c_str())) {
                const auto result =
                    components.add_default(world, entity, component.type);
                record_component_result(
                    activity,
                    "AddComponent",
                    entity,
                    component,
                    result
                );
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void sync_scene_texture(
    SpriteOutput& output,
    ImGuiTextureRegistry& textures,
    EditorUiState& state
) {
    if (state.registered_scene_texture == output.texture) {
        return;
    }
    if (state.scene_texture_id != ImTextureID_Invalid) {
        textures.unregister_texture(state.scene_texture_id);
        state.scene_texture_id = ImTextureID_Invalid;
    }
    state.registered_scene_texture = output.texture;
    if (output.texture) {
        state.scene_texture_id = textures.register_texture(output.texture);
    }
}

void draw_scene(
    SpriteOutput& output,
    ImGuiTextureRegistry& textures,
    EditorUiState& state
) {
    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    const auto available = ImGui::GetContentRegionAvail();
    const auto width = static_cast<uint32>(std::max(available.x, 1.0f));
    const auto height = static_cast<uint32>(std::max(available.y, 1.0f));
    if (output.requested_width != width || output.requested_height != height) {
        output.resize(width, height);
    }

    sync_scene_texture(output, textures, state);
    if (state.scene_texture_id == ImTextureID_Invalid) {
        ImGui::TextDisabled("Waiting for the 2D render target...");
    } else {
        ImGui::Image(state.scene_texture_id, available);
    }
    ImGui::End();
}

void draw_activity(
    const ActivityLog& activity,
    const ExternalAgentStatus& agent,
    const ComponentRegistry& components
) {
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
    ImGui::TextDisabled(
        "| generic component API: %zu types",
        components.entries().size()
    );
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

void draw_editor(WorldRef world_ref) {
    auto& world = *world_ref;
    auto& state = world.resource<EditorUiState>();
    auto& selection = world.resource<Selection>();
    auto& activity = world.resource<ActivityLog>();
    auto& output = world.resource<SpriteOutput>();
    auto& textures = world.resource<ImGuiTextureRegistry>();
    const auto& components =
        static_cast<const World&>(world).resource<ComponentRegistry>();
    const auto& agent =
        static_cast<const World&>(world).resource<ExternalAgentStatus>();

    initialize_style(state);
    draw_main_menu(world, state);

    const ImGuiID dockspace = ImGui::GetID("FeiEditorDockspace");
    build_default_layout(dockspace, state);
    ImGui::DockSpaceOverViewport(
        dockspace,
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode
    );

    if (state.show_hierarchy) {
        draw_hierarchy(world, selection, activity);
    }
    if (state.show_inspector) {
        draw_inspector(world, selection, components, activity);
    }
    if (state.show_scene) {
        draw_scene(output, textures, state);
    }
    if (state.show_activity) {
        draw_activity(activity, agent, components);
    }
}

void setup_welcome_scene(
    ResRW<AssetServer> assets,
    ResRW<Selection> selection,
    ResRW<ActivityLog> activity,
    Commands commands
) {
    commands.spawn().add(
        Camera2d {
            .vertical_size = 7.0f,
            .clear_color = {0.035f, 0.045f, 0.065f, 1.0f},
        },
        Transform2d {}
    );

    const auto root =
        commands.spawn().add(Transform2d {.position = {0.0f, 0.0f}});
    const auto image = assets->load<Image>("awesomeface.png");
    auto sprite = commands.spawn().add(
        Sprite {
            .image = image,
            .size = {2.4f, 2.4f},
        },
        Transform2d {}
    );
    sprite.set_parent(root.id());
    selection->entity = sprite.id();
    activity->record(
        OperationSource::Editor,
        "CreateWelcomeScene",
        "Camera and one child entity"
    );
}

} // namespace

void EditorPlugin::setup(App& app) {
    if (!app.has_plugin<ReflectionPlugin>()) {
        fatal("EditorPlugin requires ReflectionPlugin to be installed first");
    }
    if (!app.has_plugin<RenderingPlugin>()) {
        fatal("EditorPlugin requires RenderingPlugin to be installed first");
    }
    if (!app.has_plugin<SpritePlugin>()) {
        fatal("EditorPlugin requires SpritePlugin to be installed first");
    }
    if (!app.has_plugin<ImGuiPlugin>()) {
        fatal("EditorPlugin requires ImGuiPlugin to be installed first");
    }
    if (!app.has_resource<SpriteOutput>() ||
        app.resource<SpriteOutput>().mode != SpriteOutputMode::Texture) {
        fatal("EditorPlugin requires SpritePlugin texture output mode");
    }

    app.add_resource(ComponentRegistry {})
        .add_resource(ActivityLog {})
        .add_resource(ExternalAgentStatus {})
        .add_resource(Selection {})
        .add_resource(EditorUiState {});

    auto& components = app.resource<ComponentRegistry>();
    components.register_component<Transform2d>("Transform 2D");
    components.register_component<Camera2d>("Camera 2D");
    components.register_component<Sprite>("Sprite");
    if (!register_asset_handle_codec<Image>(
            components.codecs(),
            app.resource<AssetServer>(),
            app.resource<Assets<Image>>()
        )) {
        fatal("EditorPlugin failed to register the Image handle codec");
    }

    app.resource<ActivityLog>().record(
        OperationSource::Editor,
        "StartEditor",
        "External-agent mode; no internal agent"
    );

    if (m_config.create_welcome_scene) {
        app.add_systems(PreStartUp, setup_welcome_scene);
    }
    app.add_systems(
        RenderUpdate,
        draw_editor | in_set<RenderingSystems::Render>() | main_thread()
    );
}

void EditorPlugin::cleanup(App& app) noexcept {
    if (!app.has_resource<EditorUiState>() ||
        !app.has_resource<ImGuiTextureRegistry>()) {
        return;
    }

    auto& state = app.resource<EditorUiState>();
    if (state.scene_texture_id != ImTextureID_Invalid) {
        app.resource<ImGuiTextureRegistry>().unregister_texture(
            state.scene_texture_id
        );
        state.scene_texture_id = ImTextureID_Invalid;
        state.registered_scene_texture.reset();
    }
}

} // namespace fei::editor
