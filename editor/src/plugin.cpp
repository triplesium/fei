#include "editor/plugin.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/assets.hpp"
#include "asset/serialization.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/type_tags.hpp"
#include "ecs/world.hpp"
#include "editor/activity.hpp"
#include "editor/asset_browser.hpp"
#include "editor/component_operations.hpp"
#include "imgui/plugin.hpp"
#include "imgui/renderer.hpp"
#include "refl/cls.hpp"
#include "refl/property.hpp"
#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_asset.hpp"
#include "sprite/components.hpp"
#include "sprite/output.hpp"
#include "sprite/plugin.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
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
    ImTextureID asset_preview_texture_id {ImTextureID_Invalid};
    std::shared_ptr<const Texture> registered_asset_preview_texture;
    Optional<AssetPath> asset_preview_path;
    Handle<Image> asset_preview_handle;
    bool layout_initialized {false};
    bool style_initialized {false};
    bool show_scene {true};
    bool show_hierarchy {true};
    bool show_inspector {true};
    bool show_assets {true};
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

struct ReflectedComponent {
    TypeId type;
    std::string name;
    bool default_constructible {false};
};

std::vector<ReflectedComponent>
reflected_components(const std::vector<TypeId>& types) {
    std::vector<ReflectedComponent> components;
    components.reserve(types.size());
    for (const auto type : types) {
        auto reflected_type = Registry::instance().try_get_type(type);
        if (!reflected_type || !reflected_type->has_tag(ComponentTypeTag)) {
            continue;
        }
        components.push_back(
            ReflectedComponent {
                .type = type,
                .name = reflected_type->stripped_name(),
                .default_constructible =
                    reflected_type->default_constructible(),
            }
        );
    }
    std::ranges::sort(components, {}, &ReflectedComponent::name);
    return components;
}

std::vector<ReflectedComponent> reflected_components() {
    return reflected_components(
        Registry::instance().types_with_tag(ComponentTypeTag)
    );
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
    ImGuiID assets = bottom;
    ImGuiID activity = 0;
    ImGui::DockBuilderSplitNode(
        assets,
        ImGuiDir_Right,
        0.42f,
        &activity,
        &assets
    );

    ImGui::DockBuilderDockWindow("Assets", assets);
    ImGui::DockBuilderDockWindow("Agent Activity", activity);
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
        ImGui::MenuItem("Assets", nullptr, &state.show_assets);
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
    const ComponentOperations& operations
);

bool draw_reflected_object(
    Ref value,
    int depth,
    const ComponentOperations& operations
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
            operations
        );
    }
    return changed;
}

bool draw_reflected_value(
    Ref value,
    std::string_view label,
    int depth,
    const ComponentOperations& operations
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
    } else if (auto preview = operations.preview(value)) {
        draw_field_label(label);
        ImGui::TextUnformatted(preview->c_str());
    } else if (Registry::instance().try_get_cls(value.type_id())) {
        const auto flags =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool open = ImGui::TreeNodeEx(label_string.c_str(), flags);
        if (open) {
            changed = draw_reflected_object(value, depth, operations);
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
    std::string_view component_name,
    const Status<ComponentError>& result
) {
    const auto detail = "Entity " + std::to_string(entity) + " / " +
                        std::string(component_name) +
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
    const ComponentOperations& operations,
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
    const auto location = world.entity_location(entity);
    const auto component_list = reflected_components(
        world.archetypes().get(location->archetype_id).components()
    );
    for (const auto& component : component_list) {

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
            if (draw_reflected_object(value, 0, operations)) {
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
        const auto component_name = reflected_type_name(*pending_remove);
        const auto result = operations.remove(world, entity, *pending_remove);
        record_component_result(
            activity,
            "RemoveComponent",
            entity,
            component_name,
            result
        );
    }

    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("add_component");
    }
    if (ImGui::BeginPopup("add_component")) {
        for (const auto& component : reflected_components()) {
            if (world.has_component(entity, component.type)) {
                continue;
            }
            if (ImGui::MenuItem(
                    component.name.c_str(),
                    nullptr,
                    false,
                    component.default_constructible
                )) {
                const auto result =
                    operations.add_default(world, entity, component.type);
                record_component_result(
                    activity,
                    "AddComponent",
                    entity,
                    component.name,
                    result
                );
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

std::string asset_type_label(const AssetEntry& entry) {
    if (entry.kind == AssetEntryKind::Directory) {
        return "Folder";
    }
    auto extension = entry.path.path().extension().string();
    if (extension.empty()) {
        return "File";
    }
    extension.erase(extension.begin());
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::toupper(value));
        }
    );
    return extension;
}

std::string asset_size_label(std::uintmax_t size) {
    constexpr std::string_view units[] = {"B", "KiB", "MiB", "GiB"};
    auto value = static_cast<double>(size);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? std::format("{} B", size) :
                       std::format("{:.1f} {}", value, units[unit]);
}

bool is_previewable_image(const AssetEntry& entry) {
    if (entry.kind != AssetEntryKind::File) {
        return false;
    }
    auto extension = entry.path.path().extension().string();
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".bmp" || extension == ".tga" || extension == ".hdr";
}

void clear_asset_preview(ImGuiTextureRegistry& textures, EditorUiState& state) {
    if (state.asset_preview_texture_id != ImTextureID_Invalid) {
        textures.unregister_texture(state.asset_preview_texture_id);
    }
    state.asset_preview_texture_id = ImTextureID_Invalid;
    state.registered_asset_preview_texture.reset();
    state.asset_preview_path = nullopt;
    state.asset_preview_handle = {};
}

void sync_asset_preview(
    const AssetEntry* selected,
    AssetServer& asset_server,
    RenderAssets<GpuImage>& gpu_images,
    ImGuiTextureRegistry& textures,
    EditorUiState& state
) {
    Optional<AssetPath> preview_path;
    if (selected && is_previewable_image(*selected)) {
        preview_path = selected->path;
    }
    if (preview_path != state.asset_preview_path) {
        clear_asset_preview(textures, state);
        state.asset_preview_path = preview_path;
        if (preview_path) {
            state.asset_preview_handle =
                asset_server.load<Image>(*preview_path);
        }
    }

    if (!state.asset_preview_handle) {
        return;
    }
    const auto gpu_image = gpu_images.get(state.asset_preview_handle);
    if (!gpu_image || !gpu_image->texture() || !gpu_image->sampler()) {
        return;
    }

    const auto texture = gpu_image->texture();
    if (state.registered_asset_preview_texture == texture) {
        return;
    }
    if (state.asset_preview_texture_id != ImTextureID_Invalid) {
        textures.unregister_texture(state.asset_preview_texture_id);
    }
    state.registered_asset_preview_texture = texture;
    state.asset_preview_texture_id =
        textures.register_texture(texture, gpu_image->sampler());
}

bool draw_asset_breadcrumb(AssetBrowser& browser) {
    bool navigated = false;
    const auto root_label = browser.root().source() ?
                                *browser.root().source() + "://" :
                                std::string("/");
    if (ImGui::SmallButton(root_label.c_str())) {
        navigated |= browser.navigate_to(browser.root());
    }

    auto accumulated = browser.root().path();
    const auto current = browser.current_directory().path();
    const auto relative = current.lexically_relative(browser.root().path());
    int component_index = 0;
    for (const auto& component : relative) {
        accumulated /= component;
        ImGui::SameLine(0.0f, 3.0f);
        ImGui::TextUnformatted("/");
        ImGui::SameLine(0.0f, 3.0f);
        ImGui::PushID(component_index++);
        if (ImGui::SmallButton(component.string().c_str())) {
            auto target = AssetPath(accumulated);
            if (browser.root().source()) {
                target = target.with_source(*browser.root().source());
            }
            navigated |= browser.navigate_to(target);
        }
        ImGui::PopID();
    }
    return navigated;
}

void draw_asset_details(
    const AssetBrowser& browser,
    AssetServer& asset_server,
    const Assets<Image>& images,
    RenderAssets<GpuImage>& gpu_images,
    ImGuiTextureRegistry& textures,
    EditorUiState& state
) {
    const auto* selected = browser.selected_entry();
    sync_asset_preview(selected, asset_server, gpu_images, textures, state);
    if (!selected) {
        ImGui::TextDisabled("Select an asset to inspect it.");
        return;
    }

    ImGui::TextUnformatted(selected->path.as_string().c_str());
    ImGui::TextDisabled("Type: %s", asset_type_label(*selected).c_str());
    if (selected->kind == AssetEntryKind::File) {
        ImGui::SameLine();
        ImGui::TextDisabled(
            "| Size: %s",
            asset_size_label(selected->size).c_str()
        );
    }

    if (!is_previewable_image(*selected)) {
        return;
    }
    if (auto load_error = asset_server.load_error(state.asset_preview_handle)) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            load_error->message.c_str()
        );
        return;
    }

    const auto image = images.get(state.asset_preview_handle);
    if (!image) {
        ImGui::TextDisabled("Loading image preview...");
        return;
    }
    ImGui::TextDisabled(
        "%u x %u | %u channels",
        image->width(),
        image->height(),
        image->channels()
    );

    if (state.asset_preview_texture_id == ImTextureID_Invalid ||
        image->width() == 0 || image->height() == 0) {
        ImGui::TextDisabled("Preparing GPU preview...");
        return;
    }
    const auto available = ImGui::GetContentRegionAvail();
    const auto scale = std::min({
        available.x / static_cast<float>(image->width()),
        120.0f / static_cast<float>(image->height()),
        1.0f,
    });
    ImGui::Image(
        state.asset_preview_texture_id,
        ImVec2 {
            static_cast<float>(image->width()) * scale,
            static_cast<float>(image->height()) * scale,
        },
        ImVec2 {0.0f, 1.0f},
        ImVec2 {1.0f, 0.0f}
    );
}

void draw_assets(
    AssetBrowser& browser,
    AssetServer& asset_server,
    const Assets<Image>& images,
    RenderAssets<GpuImage>& gpu_images,
    ImGuiTextureRegistry& textures,
    EditorUiState& state
) {
    if (!ImGui::Begin("Assets")) {
        ImGui::End();
        return;
    }

    ImGui::BeginDisabled(browser.current_directory() == browser.root());
    if (ImGui::SmallButton("Up")) {
        browser.navigate_up();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        browser.request_refresh();
    }
    ImGui::SameLine();
    draw_asset_breadcrumb(browser);

    if (browser.refresh_requested()) {
        browser.refresh(asset_server);
    }
    if (browser.error()) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            browser.error()->c_str()
        );
    }

    bool navigated = false;
    const auto table_height =
        std::max(ImGui::GetContentRegionAvail().y - 150.0f, 120.0f);
    if (ImGui::BeginTable(
            "asset_entries",
            3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2 {0.0f, table_height}
        )) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Type",
            ImGuiTableColumnFlags_WidthFixed,
            72.0f
        );
        ImGui::TableSetupColumn(
            "Size",
            ImGuiTableColumnFlags_WidthFixed,
            84.0f
        );
        ImGui::TableHeadersRow();

        for (const auto& entry : browser.entries()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(entry.path.as_string().c_str());
            const auto filename = entry.path.path().filename().string();
            const bool selected =
                browser.selection() && *browser.selection() == entry.path;
            if (ImGui::Selectable(
                    filename.c_str(),
                    selected,
                    ImGuiSelectableFlags_SpanAllColumns |
                        ImGuiSelectableFlags_AllowDoubleClick
                )) {
                browser.select(entry);
                if (entry.kind == AssetEntryKind::Directory &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    navigated |= browser.open(entry);
                }
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(asset_type_label(entry).c_str());
            ImGui::TableSetColumnIndex(2);
            if (entry.kind == AssetEntryKind::File) {
                ImGui::TextUnformatted(asset_size_label(entry.size).c_str());
            }
        }
        ImGui::EndTable();
    }
    if (navigated && browser.refresh_requested()) {
        browser.refresh(asset_server);
    }

    draw_asset_details(
        browser,
        asset_server,
        images,
        gpu_images,
        textures,
        state
    );
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
        ImGui::Image(
            state.scene_texture_id,
            available,
            ImVec2 {0.0f, 1.0f},
            ImVec2 {1.0f, 0.0f}
        );
    }
    ImGui::End();
}

void draw_activity(
    const ActivityLog& activity,
    const ExternalAgentStatus& agent
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
    const auto component_count =
        Registry::instance().types_with_tag(ComponentTypeTag).size();
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

void draw_editor(WorldRef world_ref) {
    auto& world = *world_ref;
    auto& state = world.resource<EditorUiState>();
    auto& selection = world.resource<Selection>();
    auto& activity = world.resource<ActivityLog>();
    auto& asset_browser = world.resource<AssetBrowser>();
    auto& asset_server = world.resource<AssetServer>();
    const auto& images =
        static_cast<const World&>(world).resource<Assets<Image>>();
    auto& gpu_images = world.resource<RenderAssets<GpuImage>>();
    auto& output = world.resource<SpriteOutput>();
    auto& textures = world.resource<ImGuiTextureRegistry>();
    const auto& operations =
        static_cast<const World&>(world).resource<ComponentOperations>();
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
        draw_inspector(world, selection, operations, activity);
    }
    if (state.show_scene) {
        draw_scene(output, textures, state);
    }
    if (state.show_assets) {
        draw_assets(
            asset_browser,
            asset_server,
            images,
            gpu_images,
            textures,
            state
        );
    }
    if (state.show_activity) {
        draw_activity(activity, agent);
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

    app.add_resource(ComponentOperations {})
        .add_resource(ActivityLog {})
        .add_resource(AssetBrowser {})
        .add_resource(ExternalAgentStatus {})
        .add_resource(Selection {})
        .add_resource(EditorUiState {});

    auto& operations = app.resource<ComponentOperations>();
    if (!register_asset_handle_codec<Image>(
            operations.codecs(),
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
    clear_asset_preview(app.resource<ImGuiTextureRegistry>(), state);
}

} // namespace fei::editor
