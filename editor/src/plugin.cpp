#include "editor/plugin.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/assets.hpp"
#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/plugin.hpp"
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
#include "editor/asset_watcher.hpp"
#include "editor/component_operations.hpp"
#include "imgui/plugin.hpp"
#include "imgui/renderer.hpp"
#include "imgui/texture.hpp"
#include "project/project.hpp"
#include "refl/cls.hpp"
#include "refl/property.hpp"
#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "scene/document.hpp"
#include "serialization/json_archive.hpp"
#include "sprite/components.hpp"
#include "sprite/output.hpp"
#include "sprite/plugin.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fei::editor {

namespace {

enum class AssetClipboardOperation : std::uint8_t {
    Cut,
    Copy,
};

struct AssetClipboard {
    AssetClipboardOperation operation;
    AssetPath source;
};

struct EditorUiState {
    ImGuiTextureHandle scene_texture;
    uint32 scene_width {1280};
    uint32 scene_height {720};
    ImGuiTextureHandle asset_preview_texture;
    Optional<AssetPath> asset_preview_path;
    Handle<Image> asset_preview_handle;
    bool layout_initialized {false};
    bool style_initialized {false};
    bool show_scene {true};
    bool show_hierarchy {true};
    bool show_inspector {true};
    bool show_assets {true};
    bool show_activity {true};
    std::array<char, 1024> import_source {};
    std::array<char, 512> import_destination {};
    Optional<std::string> import_error;
    Optional<AssetPath> import_settings_path;
    AssetImportSettings import_settings;
    bool import_settings_dirty {false};
    Optional<std::string> import_settings_error;
    std::array<char, 256> rename_name {};
    Optional<AssetPath> rename_source;
    Optional<std::string> rename_error;
    Optional<AssetPath> reveal_asset_path;
    Optional<AssetClipboard> asset_clipboard;
    std::array<char, 256> create_folder_name {};
    Optional<AssetPath> create_folder_parent;
    Optional<std::string> asset_operation_error;
    Optional<AssetPath> delete_target;
    bool delete_target_is_directory {false};
    Optional<std::string> delete_error;
};

struct ExtractedEditorViewport {
    ImGuiTextureHandle texture;
    uint32 width {0};
    uint32 height {0};
};

struct EditorRenderSystems {
    struct BindViewport : SystemSet<BindViewport> {};
};

void extract_editor_viewport(
    Extract<ResRO<EditorUiState>> editor,
    ResRW<ExtractedEditorViewport> viewport,
    ResRW<SpriteOutput> output
) {
    viewport->texture = (*editor)->scene_texture;
    viewport->width = (*editor)->show_scene ? (*editor)->scene_width : 0;
    viewport->height = (*editor)->show_scene ? (*editor)->scene_height : 0;
    output->resize(viewport->width, viewport->height);
}

void bind_editor_viewport(
    ResRO<ExtractedEditorViewport> viewport,
    ResRO<SpriteOutput> output,
    ResRW<ImGuiTextureRegistry> textures
) {
    if (!viewport->texture) {
        return;
    }
    if (!output->texture) {
        textures->unbind_render_texture(viewport->texture);
        return;
    }
    textures->bind_render_texture(viewport->texture, output->texture);
}

void shutdown_editor_viewport(World& world) {
    if (!world.has_resource<ExtractedEditorViewport>() ||
        !world.has_resource<ImGuiTextureRegistry>()) {
        return;
    }
    world.resource<ImGuiTextureRegistry>().unbind_render_texture(
        world.resource<ExtractedEditorViewport>().texture
    );
}

struct AssetWatchSyncState {
    Optional<std::string> last_error;
};

struct EditorSceneSession {
    Optional<AssetPath> path;
    Optional<SceneDocument> document;
    SceneEntityBindings bindings;
    bool dirty {false};
    bool external_change_pending {false};
    Optional<std::string> error;
};

bool is_scene_document_path(const AssetPath& path) {
    return path.path().filename().string().ends_with(".scene.yaml");
}

Status<std::string> write_file_atomically(
    const std::filesystem::path& path,
    std::string_view contents
) {
    auto temporary = path;
    temporary += ".saving";
    auto backup = path;
    backup += ".backup";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return failure(
                "Failed to create temporary scene file: " + temporary.string()
            );
        }
        stream.write(
            contents.data(),
            static_cast<std::streamsize>(contents.size())
        );
        if (!stream) {
            return failure(
                "Failed to write temporary scene file: " + temporary.string()
            );
        }
    }

    std::error_code error;
    std::filesystem::remove(backup, error);
    error.clear();
    const bool had_original = std::filesystem::exists(path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return failure("Failed to inspect scene file: " + error.message());
    }
    if (had_original) {
        std::filesystem::rename(path, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return failure(
                "Failed to stage old scene file: " + error.message()
            );
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        if (had_original) {
            std::error_code rollback_error;
            std::filesystem::rename(backup, path, rollback_error);
        }
        std::filesystem::remove(temporary, error);
        return failure("Failed to publish scene file: " + error.message());
    }
    std::filesystem::remove(backup, error);
    return {};
}

bool serialized_nodes_equal(
    const serialization::SerializedNode& lhs,
    const serialization::SerializedNode& rhs
) {
    const auto lhs_json = serialization::write_json(lhs, 0);
    const auto rhs_json = serialization::write_json(rhs, 0);
    return lhs_json && rhs_json && *lhs_json == *rhs_json;
}

void record_scene_diff(
    const Optional<SceneDocument>& previous,
    const SceneDocument& current,
    ActivityLog& activity
) {
    if (!previous) {
        activity.record(
            OperationSource::ExternalAgent,
            "OpenScene",
            std::to_string(current.entities.size()) + " entities"
        );
        return;
    }
    std::unordered_map<AssetUuid, const SceneEntityDocument*> old_entities;
    std::unordered_map<AssetUuid, const SceneEntityDocument*> new_entities;
    for (const auto& entity : previous->entities) {
        old_entities.emplace(entity.id, &entity);
    }
    for (const auto& entity : current.entities) {
        new_entities.emplace(entity.id, &entity);
    }
    for (const auto& [id, entity] : old_entities) {
        (void)entity;
        if (!new_entities.contains(id)) {
            activity.record(
                OperationSource::ExternalAgent,
                "DeleteEntity",
                id.as_string()
            );
        }
    }
    for (const auto& [id, entity] : new_entities) {
        const auto old = old_entities.find(id);
        if (old == old_entities.end()) {
            activity.record(
                OperationSource::ExternalAgent,
                "CreateEntity",
                id.as_string()
            );
            continue;
        }
        if (entity->parent != old->second->parent) {
            activity.record(
                OperationSource::ExternalAgent,
                "ReparentEntity",
                id.as_string()
            );
        }
        std::unordered_map<std::string, const SceneComponentDocument*>
            old_components;
        std::unordered_map<std::string, const SceneComponentDocument*>
            new_components;
        for (const auto& component : old->second->components) {
            old_components.emplace(component.type, &component);
        }
        for (const auto& component : entity->components) {
            new_components.emplace(component.type, &component);
        }
        for (const auto& [type, component] : old_components) {
            (void)component;
            if (!new_components.contains(type)) {
                activity.record(
                    OperationSource::ExternalAgent,
                    "RemoveComponent",
                    id.as_string() + " / " + type
                );
            }
        }
        for (const auto& [type, component] : new_components) {
            const auto old_component = old_components.find(type);
            if (old_component == old_components.end()) {
                activity.record(
                    OperationSource::ExternalAgent,
                    "AddComponent",
                    id.as_string() + " / " + type
                );
            } else if (!serialized_nodes_equal(
                           component->properties,
                           old_component->second->properties
                       )) {
                activity.record(
                    OperationSource::ExternalAgent,
                    "SetComponentProperty",
                    id.as_string() + " / " + type
                );
            }
        }
    }
}

Status<std::string> replace_editor_scene(
    World& world,
    const AssetPath& path,
    SceneDocument document,
    const ComponentOperations& operations,
    Selection& selection,
    ActivityLog& activity,
    EditorSceneSession& session,
    OperationSource source
) {
    auto instantiated =
        instantiate_scene_document(document, world, &operations.codecs());
    if (!instantiated) {
        return failure(
            instantiated.error().path + ": " + instantiated.error().message
        );
    }

    const auto old_entities = session.bindings.entities();
    for (const auto entity : old_entities) {
        if (world.has_entity(entity)) {
            world.despawn(entity);
        }
    }
    if (source == OperationSource::ExternalAgent) {
        record_scene_diff(session.document, document, activity);
    }
    session.path = path;
    session.document = std::move(document);
    session.bindings = std::move(instantiated->bindings);
    session.dirty = false;
    session.external_change_pending = false;
    session.error = nullopt;
    selection.entity = nullopt;
    for (const auto& warning : instantiated->warnings) {
        activity.record(source, "LoadSceneComponent", warning, false);
    }
    return {};
}

Result<SceneDocument, std::string>
read_scene_document(const AssetPath& path, const AssetDatabase& database) {
    auto file = database.resolve(path);
    if (!file) {
        return failure(std::move(file.error()));
    }
    auto reader = Reader::from_file(*file);
    if (!reader) {
        return failure(std::move(reader.error().message));
    }
    auto document = parse_scene_document(reader->as_string_view());
    if (!document) {
        return failure(document.error().path + ": " + document.error().message);
    }
    return std::move(*document);
}

Status<std::string> save_editor_scene(
    World& world,
    AssetDatabase& database,
    ProjectAssetWatcher& watcher,
    const ComponentOperations& operations,
    ActivityLog& activity,
    EditorSceneSession& session
) {
    if (!session.path) {
        return failure(std::string("The current scene has no asset path"));
    }
    auto document = capture_scene_document(
        world,
        session.bindings,
        &operations.codecs(),
        session.document ? &*session.document : nullptr
    );
    if (!document) {
        return failure(document.error().path + ": " + document.error().message);
    }
    auto encoded = write_scene_document(*document);
    if (!encoded) {
        return failure(encoded.error().path + ": " + encoded.error().message);
    }
    auto file = database.resolve(*session.path);
    if (!file) {
        return failure(std::move(file.error()));
    }
    std::error_code directory_error;
    std::filesystem::create_directories(file->parent_path(), directory_error);
    if (directory_error) {
        return failure(
            "Failed to create scene directory: " + directory_error.message()
        );
    }
    if (auto status = write_file_atomically(*file, *encoded); !status) {
        return status;
    }
    auto metadata = database.ensure_native_asset(*session.path);
    if (!metadata) {
        return failure(std::move(metadata.error()));
    }
    session.document = std::move(*document);
    session.dirty = false;
    session.external_change_pending = false;
    session.error = nullopt;
    if (auto status = watcher.acknowledge(); !status) {
        return status;
    }
    activity
        .record(OperationSource::User, "SaveScene", session.path->as_string());
    return {};
}

AssetPath next_untitled_scene_path(const AssetDatabase& database) {
    for (std::uint32_t index = 1;; ++index) {
        const auto name =
            index == 1 ? "untitled.scene.yaml" :
                         "untitled_" + std::to_string(index) + ".scene.yaml";
        const auto candidate = AssetPath("project://scenes/" + name);
        auto file = database.resolve(candidate);
        std::error_code error;
        if (!file || !std::filesystem::exists(*file, error)) {
            return candidate;
        }
    }
}

Status<std::string> reload_editor_scene(
    World& world,
    const AssetDatabase& database,
    const ComponentOperations& operations,
    Selection& selection,
    ActivityLog& activity,
    EditorSceneSession& session,
    OperationSource source
) {
    if (!session.path) {
        return failure(std::string("The current scene has no asset path"));
    }
    auto document = read_scene_document(*session.path, database);
    if (!document) {
        return failure(std::move(document.error()));
    }
    return replace_editor_scene(
        world,
        *session.path,
        std::move(*document),
        operations,
        selection,
        activity,
        session,
        source
    );
}

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
        auto& scene = world.resource<EditorSceneSession>();
        auto& activity = world.resource<ActivityLog>();
        auto& selection = world.resource<Selection>();
        if (ImGui::MenuItem("New Scene", nullptr, false, !scene.dirty)) {
            const auto path = next_untitled_scene_path(
                static_cast<const World&>(world).resource<AssetDatabase>()
            );
            auto status = replace_editor_scene(
                world,
                path,
                SceneDocument {},
                static_cast<const World&>(world)
                    .resource<ComponentOperations>(),
                selection,
                activity,
                scene,
                OperationSource::User
            );
            if (status) {
                scene.dirty = true;
                activity.record(
                    OperationSource::User,
                    "NewScene",
                    path.as_string()
                );
            }
        }
        const bool can_open_selected = selection.asset && !scene.dirty &&
                                       is_scene_document_path(*selection.asset);
        if (ImGui::MenuItem(
                "Open Selected Scene",
                nullptr,
                false,
                can_open_selected
            )) {
            const auto selected_path = *selection.asset;
            auto document = read_scene_document(
                selected_path,
                static_cast<const World&>(world).resource<AssetDatabase>()
            );
            Status<std::string> status =
                document ? replace_editor_scene(
                               world,
                               selected_path,
                               std::move(*document),
                               static_cast<const World&>(world)
                                   .resource<ComponentOperations>(),
                               selection,
                               activity,
                               scene,
                               OperationSource::User
                           ) :
                           failure(document.error());
            if (!status) {
                scene.error = status.error();
                activity.record(
                    OperationSource::User,
                    "OpenScene",
                    status.error(),
                    false
                );
            } else {
                activity.record(
                    OperationSource::User,
                    "OpenScene",
                    scene.path->as_string()
                );
            }
        }
        if (ImGui::MenuItem(
                "Save Scene",
                "Ctrl+S",
                false,
                scene.path.has_value()
            )) {
            auto status = save_editor_scene(
                world,
                world.resource<AssetDatabase>(),
                world.resource<ProjectAssetWatcher>(),
                static_cast<const World&>(world)
                    .resource<ComponentOperations>(),
                activity,
                scene
            );
            if (!status) {
                scene.error = status.error();
                activity.record(
                    OperationSource::User,
                    "SaveScene",
                    status.error(),
                    false
                );
            }
        }
        if (ImGui::MenuItem(
                "Reload Scene from Disk",
                nullptr,
                false,
                scene.path.has_value()
            )) {
            auto status = reload_editor_scene(
                world,
                static_cast<const World&>(world).resource<AssetDatabase>(),
                static_cast<const World&>(world)
                    .resource<ComponentOperations>(),
                selection,
                activity,
                scene,
                OperationSource::User
            );
            if (!status) {
                scene.error = status.error();
                activity.record(
                    OperationSource::User,
                    "ReloadScene",
                    status.error(),
                    false
                );
            } else {
                activity.record(
                    OperationSource::User,
                    "ReloadScene",
                    scene.path->as_string()
                );
            }
        }
        if (scene.external_change_pending &&
            ImGui::MenuItem("Keep Local Scene")) {
            scene.external_change_pending = false;
            scene.dirty = true;
            activity.record(
                OperationSource::User,
                "KeepLocalScene",
                scene.path ? scene.path->as_string() : std::string {}
            );
        }
        ImGui::Separator();
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

void draw_hierarchy(
    World& world,
    Selection& selection,
    ActivityLog& activity,
    EditorSceneSession& scene
) {
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Entity")) {
        const auto entity = world.entity();
        scene.bindings.ensure(entity);
        selection.entity = entity;
        selection.asset = nullopt;
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

void draw_asset_inspector(
    const AssetPath& path,
    AssetBrowser& browser,
    AssetServer& asset_server,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity,
    const Assets<Image>& images,
    ImGuiImages& image_textures,
    EditorUiState& state
);

void reimport_asset(
    const AssetPath& path,
    AssetBrowser& browser,
    AssetServer& asset_server,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity,
    EditorUiState& state,
    std::string_view action,
    bool use_edited_settings
);

void draw_asset_preview(
    const AssetPath& path,
    AssetServer& asset_server,
    const Assets<Image>& images,
    ImGuiImages& image_textures,
    EditorUiState& state
);

void draw_inspector(
    World& world,
    Selection& selection,
    const ComponentOperations& operations,
    ActivityLog& activity,
    AssetBrowser& browser,
    AssetServer& asset_server,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    const Assets<Image>& images,
    ImGuiImages& image_textures,
    EditorUiState& state
) {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    if (selection.entity && !world.has_entity(*selection.entity)) {
        selection.entity = nullopt;
    }
    if (!selection.entity && selection.asset) {
        draw_asset_inspector(
            *selection.asset,
            browser,
            asset_server,
            importers,
            database,
            activity,
            images,
            image_textures,
            state
        );
        ImGui::End();
        return;
    }
    if (!selection.entity) {
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

const char* asset_import_state_label(AssetImportState state) {
    switch (state) {
        case AssetImportState::Unimported:
            return "Unimported";
        case AssetImportState::Imported:
            return "Imported";
        case AssetImportState::Failed:
            return "Failed";
    }
    return "Unknown";
}

template<std::size_t Size>
void set_text_buffer(std::array<char, Size>& buffer, std::string_view value) {
    const auto length = std::min(value.size(), buffer.size() - 1);
    std::ranges::copy_n(value.begin(), length, buffer.begin());
    buffer[length] = '\0';
}

bool draw_import_setting(std::string_view name, std::string& value) {
    ImGui::PushID(name.data(), name.data() + name.size());
    bool changed = false;
    draw_field_label(name);
    if (value == "true" || value == "false") {
        bool enabled = value == "true";
        if (ImGui::Checkbox("##value", &enabled)) {
            value = enabled ? "true" : "false";
            changed = true;
        }
    } else if (value == "linear" || value == "srgb") {
        if (ImGui::BeginCombo("##value", value.c_str())) {
            for (const auto option : {"linear", "srgb"}) {
                const bool selected = value == option;
                if (ImGui::Selectable(option, selected)) {
                    value = option;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    } else {
        std::array<char, 256> buffer {};
        set_text_buffer(buffer, value);
        if (ImGui::InputText("##value", buffer.data(), buffer.size())) {
            value = buffer.data();
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

void draw_asset_inspector(
    const AssetPath& path,
    AssetBrowser& browser,
    AssetServer& asset_server,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity,
    const Assets<Image>& images,
    ImGuiImages& image_textures,
    EditorUiState& state
) {
    const auto* metadata = database.metadata(path);
    const auto* importer = metadata ? importers.find(metadata->importer) :
                                      importers.find_for(path.path());
    const auto current_settings = metadata ? metadata->settings :
                                  importer ? importer->default_settings(path) :
                                             AssetImportSettings {};
    if (!state.import_settings_path || *state.import_settings_path != path ||
        (!state.import_settings_dirty &&
         state.import_settings != current_settings)) {
        state.import_settings_path = path;
        state.import_settings = current_settings;
        state.import_settings_dirty = false;
        state.import_settings_error = nullopt;
    }

    ImGui::TextUnformatted(path.as_string().c_str());
    ImGui::Separator();
    ImGui::TextDisabled(
        "Status: %s",
        asset_import_state_label(database.state(path))
    );
    if (auto source = database.resolve(path)) {
        ImGui::TextWrapped("Source: %s", source->string().c_str());
    }
    if (metadata) {
        ImGui::TextDisabled("UUID: %s", metadata->id.as_string().c_str());
        if (const auto* record = database.import_record(path)) {
            ImGui::TextDisabled(
                "Importer: %s v%u%s",
                metadata->importer.c_str(),
                record->importer_version,
                importer && record->importer_version != importer->version() ?
                    " (outdated)" :
                    ""
            );
            ImGui::TextDisabled("Source hash: %s", record->source_hash.c_str());
            if (ImGui::CollapsingHeader(
                    "Artifacts",
                    ImGuiTreeNodeFlags_DefaultOpen
                )) {
                if (record->artifacts.empty()) {
                    ImGui::TextDisabled("No generated artifacts");
                }
                for (const auto& artifact : record->artifacts) {
                    ImGui::BulletText(
                        "%s: %s",
                        artifact.kind.c_str(),
                        artifact.path.generic_string().c_str()
                    );
                    if (auto artifact_file =
                            database.artifact_path(path, artifact.kind)) {
                        ImGui::TextWrapped(
                            "  %s",
                            artifact_file->string().c_str()
                        );
                    }
                }
            }
        } else {
            ImGui::TextDisabled(
                "Importer: %s (not imported)",
                metadata->importer.c_str()
            );
        }
    } else if (importer) {
        const auto importer_name = std::string(importer->name());
        ImGui::TextDisabled(
            "Importer: %s v%u",
            importer_name.c_str(),
            importer->version()
        );
    } else {
        ImGui::TextDisabled("No importer registered for this type");
    }

    if (const auto error = database.error(path)) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            error->c_str()
        );
    }
    if (state.import_settings_error) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            state.import_settings_error->c_str()
        );
    }

    if (!state.import_settings.empty() && ImGui::CollapsingHeader(
                                              "Import Settings",
                                              ImGuiTreeNodeFlags_DefaultOpen
                                          )) {
        for (auto& [name, value] : state.import_settings) {
            state.import_settings_dirty |= draw_import_setting(name, value);
        }
    }

    if (state.import_settings_dirty) {
        ImGui::BeginDisabled(!importer);
        if (ImGui::Button("Apply")) {
            reimport_asset(
                path,
                browser,
                asset_server,
                importers,
                database,
                activity,
                state,
                "ApplyImportSettings",
                true
            );
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            state.import_settings = current_settings;
            state.import_settings_dirty = false;
            state.import_settings_error = nullopt;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Unsaved import settings");
    }

    ImGui::Separator();
    draw_asset_preview(path, asset_server, images, image_textures, state);
}

void draw_import_popup(
    AssetBrowser& browser,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity,
    EditorUiState& state
) {
    if (!ImGui::BeginPopupModal(
            "Import Asset",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }

    ImGui::TextUnformatted("Copy an external file into this project.");
    ImGui::SetNextItemWidth(560.0f);
    if (ImGui::InputTextWithHint(
            "Source file",
            "Absolute or relative filesystem path",
            state.import_source.data(),
            state.import_source.size()
        )) {
        state.import_error = nullopt;
        if (state.import_destination.front() == '\0') {
            const auto filename =
                std::filesystem::path(state.import_source.data())
                    .filename()
                    .generic_string();
            if (!filename.empty()) {
                auto destination =
                    AssetPath(browser.current_directory().path() / filename);
                if (browser.current_directory().source()) {
                    destination = destination.with_source(
                        *browser.current_directory().source()
                    );
                }
                set_text_buffer(
                    state.import_destination,
                    destination.as_string()
                );
            }
        }
    }
    ImGui::SetNextItemWidth(560.0f);
    if (ImGui::InputTextWithHint(
            "Destination",
            "project://textures/example.png",
            state.import_destination.data(),
            state.import_destination.size()
        )) {
        state.import_error = nullopt;
    }

    if (state.import_error) {
        ImGui::PushTextWrapPos(580.0f);
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            state.import_error->c_str()
        );
        ImGui::PopTextWrapPos();
    }

    const bool has_request = state.import_source.front() != '\0' &&
                             state.import_destination.front() != '\0';
    ImGui::BeginDisabled(!has_request);
    if (ImGui::Button("Import")) {
        auto result = import_asset(
            AssetImportRequest {
                .source_file =
                    std::filesystem::path(state.import_source.data()),
                .destination = AssetPath(state.import_destination.data()),
                .settings = {},
            },
            importers,
            database
        );
        if (result) {
            activity.record(
                OperationSource::User,
                "ImportAsset",
                result->path.as_string()
            );
            browser.request_refresh();
            state.import_source.fill('\0');
            state.import_destination.fill('\0');
            state.import_error = nullopt;
            ImGui::CloseCurrentPopup();
        } else {
            state.import_error = result.error().message;
            activity.record(
                OperationSource::User,
                "ImportAsset",
                result.error().destination.as_string() + ": " +
                    result.error().message,
                false
            );
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.import_error = nullopt;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void auto_import_project_assets(
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity
) {
    auto report = import_pending_assets(importers, database);
    if (!report) {
        activity.record(
            OperationSource::Editor,
            "DiscoverAssets",
            report.error(),
            false
        );
        return;
    }
    for (const auto& imported : report->imported) {
        activity.record(
            OperationSource::Editor,
            "AutoImportAsset",
            imported.path.as_string()
        );
    }
    for (const auto& failed : report->failed) {
        activity.record(
            OperationSource::Editor,
            "AutoImportAsset",
            failed.destination.as_string() + ": " + failed.message,
            false
        );
    }
}

void sync_project_assets(
    ResRW<ProjectAssetWatcher> watcher,
    ResRW<AssetDatabase> database,
    Res<AssetImporterRegistry> importers,
    ResRW<AssetServer> asset_server,
    ResRW<AssetBrowser> browser,
    ResRW<ActivityLog> activity,
    ResRW<AssetWatchSyncState> sync_state,
    ResRW<EditorSceneSession> scene_session,
    Res<ComponentOperations> operations,
    ResRW<Selection> selection,
    WorldRef world
) {
    auto changes = watcher->poll();
    if (!changes) {
        if (!sync_state->last_error ||
            *sync_state->last_error != changes.error()) {
            activity->record(
                OperationSource::Editor,
                "WatchProjectAssets",
                changes.error(),
                false
            );
            sync_state->last_error = changes.error();
        }
        return;
    }
    sync_state->last_error = nullopt;
    if (changes->empty()) {
        return;
    }

    const auto make_path_map = [](const AssetDatabase& source) {
        std::unordered_map<AssetUuid, AssetPath> paths;
        for (const auto& [id, path] : source.registered_assets()) {
            paths.emplace(id, path);
        }
        return paths;
    };
    const auto old_paths = make_path_map(*database);
    const auto scan_status = database->scan();
    if (!scan_status) {
        activity->record(
            OperationSource::ExternalAgent,
            "ScanProjectAssets",
            scan_status.error(),
            false
        );
    }
    const auto new_paths = make_path_map(*database);
    std::unordered_set<AssetPath> handled_paths;

    for (const auto& [id, old_path] : old_paths) {
        const auto current = new_paths.find(id);
        if (current == new_paths.end()) {
            asset_server->remove_path(old_path);
            handled_paths.insert(old_path);
            activity->record(
                OperationSource::ExternalAgent,
                "DeleteAsset",
                old_path.as_string()
            );
            if (scene_session->path && *scene_session->path == old_path) {
                scene_session->error =
                    "The active scene was deleted externally";
                scene_session->external_change_pending = true;
            }
            continue;
        }
        if (current->second != old_path) {
            asset_server->remap_path(old_path, current->second);
            handled_paths.insert(old_path);
            handled_paths.insert(current->second);
            activity->record(
                OperationSource::ExternalAgent,
                "MoveAsset",
                old_path.as_string() + " -> " + current->second.as_string()
            );
            if (scene_session->path && *scene_session->path == old_path) {
                scene_session->path = current->second;
            }
        }
    }

    auto report = import_pending_assets(*importers, *database);
    if (!report) {
        activity->record(
            OperationSource::ExternalAgent,
            "ImportProjectAssets",
            report.error(),
            false
        );
    } else {
        for (const auto& imported : report->imported) {
            handled_paths.insert(imported.path);
            bool reloaded = true;
            if (imported.metadata.importer == "image") {
                auto reload =
                    asset_server->reload_if_loaded<Image>(imported.path);
                reloaded = reload.has_value();
                if (!reload) {
                    activity->record(
                        OperationSource::ExternalAgent,
                        "HotReloadAsset",
                        imported.path.as_string() + ": " +
                            reload.error().message,
                        false
                    );
                }
            }
            activity->record(
                OperationSource::ExternalAgent,
                "ImportAsset",
                imported.path.as_string(),
                reloaded
            );
        }
        for (const auto& failed : report->failed) {
            handled_paths.insert(failed.destination);
            activity->record(
                OperationSource::ExternalAgent,
                "ImportAsset",
                failed.destination.as_string() + ": " + failed.message,
                false
            );
        }
    }

    for (const auto& change : *changes) {
        if (change.path.path().extension() == ".meta" ||
            handled_paths.contains(change.path)) {
            continue;
        }
        if (change.directory && change.kind == AssetFileChangeKind::Modified) {
            continue;
        }
        if (!change.directory && scene_session->path &&
            change.path == *scene_session->path &&
            change.kind != AssetFileChangeKind::Removed) {
            handled_paths.insert(change.path);
            if (scene_session->dirty) {
                if (!scene_session->external_change_pending) {
                    activity->record(
                        OperationSource::ExternalAgent,
                        "SceneConflict",
                        change.path.as_string() +
                            " changed while local edits are unsaved",
                        false
                    );
                }
                scene_session->external_change_pending = true;
                continue;
            }
            auto status = reload_editor_scene(
                *world,
                *database,
                *operations,
                *selection,
                *activity,
                *scene_session,
                OperationSource::ExternalAgent
            );
            if (!status) {
                scene_session->error = status.error();
                activity->record(
                    OperationSource::ExternalAgent,
                    "ReloadScene",
                    change.path.as_string() + ": " + status.error(),
                    false
                );
            }
            continue;
        }
        if (change.kind == AssetFileChangeKind::Removed && !change.directory) {
            asset_server->remove_path(change.path);
        }
        const char* action = nullptr;
        if (change.directory) {
            action = change.kind == AssetFileChangeKind::Removed ?
                         "DeleteAssetFolder" :
                         "CreateAssetFolder";
        } else {
            switch (change.kind) {
                case AssetFileChangeKind::Added:
                    action = "CreateAssetFile";
                    break;
                case AssetFileChangeKind::Modified:
                    action = "ModifyAssetFile";
                    break;
                case AssetFileChangeKind::Removed:
                    action = "DeleteAssetFile";
                    break;
            }
        }
        activity->record(
            OperationSource::ExternalAgent,
            action,
            change.path.as_string()
        );
    }
    browser->request_refresh();
    if (auto acknowledge = watcher->acknowledge(); !acknowledge) {
        activity->record(
            OperationSource::Editor,
            "WatchProjectAssets",
            acknowledge.error(),
            false
        );
        sync_state->last_error = acknowledge.error();
    }
}

bool is_previewable_image(const AssetPath& path) {
    auto extension = path.path().extension().string();
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

void clear_asset_preview(ImGuiImages& images, EditorUiState& state) {
    if (state.asset_preview_texture) {
        images.unregister_image(state.asset_preview_texture);
    }
    state.asset_preview_texture = {};
    state.asset_preview_path = nullopt;
    state.asset_preview_handle = {};
}

void sync_asset_preview(
    const AssetPath& path,
    AssetServer& asset_server,
    ImGuiImages& images,
    EditorUiState& state
) {
    Optional<AssetPath> preview_path;
    if (is_previewable_image(path)) {
        preview_path = path;
    }
    if (preview_path != state.asset_preview_path) {
        clear_asset_preview(images, state);
        state.asset_preview_path = preview_path;
        if (preview_path) {
            state.asset_preview_handle =
                asset_server.load<Image>(*preview_path);
            if (state.asset_preview_handle) {
                state.asset_preview_texture =
                    images.register_image(state.asset_preview_handle);
            }
        }
    }
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

void draw_asset_preview(
    const AssetPath& path,
    AssetServer& asset_server,
    const Assets<Image>& images,
    ImGuiImages& image_textures,
    EditorUiState& state
) {
    sync_asset_preview(path, asset_server, image_textures, state);
    if (!is_previewable_image(path)) {
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

    if (!state.asset_preview_texture || image->width() == 0 ||
        image->height() == 0) {
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
        state.asset_preview_texture.texture_id(),
        ImVec2 {
            static_cast<float>(image->width()) * scale,
            static_cast<float>(image->height()) * scale,
        },
        ImVec2 {0.0f, 1.0f},
        ImVec2 {1.0f, 0.0f}
    );
}

void reimport_asset(
    const AssetPath& path,
    AssetBrowser& browser,
    AssetServer& asset_server,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity,
    EditorUiState& state,
    std::string_view action,
    bool use_edited_settings
) {
    const auto* metadata = database.metadata(path);
    const auto* importer = metadata ? importers.find(metadata->importer) :
                                      importers.find_for(path.path());
    if (!importer) {
        return;
    }
    auto settings =
        metadata ? metadata->settings : importer->default_settings(path);
    const bool has_edited_settings = state.import_settings_path &&
                                     *state.import_settings_path == path &&
                                     state.import_settings_dirty;
    if (use_edited_settings && state.import_settings_path &&
        *state.import_settings_path == path) {
        settings = state.import_settings;
    }

    auto source = database.resolve(path);
    if (!source) {
        state.import_settings_error = source.error();
        activity.record(
            OperationSource::User,
            std::string(action),
            path.as_string() + ": " + source.error(),
            false
        );
        return;
    }
    auto result = import_asset(
        AssetImportRequest {
            .source_file = *source,
            .destination = path,
            .settings = std::move(settings),
        },
        importers,
        database
    );
    if (!result) {
        state.import_settings_error = result.error().message;
        activity.record(
            OperationSource::User,
            std::string(action),
            path.as_string() + ": " + result.error().message,
            false
        );
        return;
    }

    if (use_edited_settings || !has_edited_settings) {
        state.import_settings_path = path;
        state.import_settings = result->metadata.settings;
        state.import_settings_dirty = false;
    }
    state.import_settings_error = nullopt;
    browser.request_refresh();
    bool reloaded = true;
    if (result->metadata.importer == "image") {
        auto reload = asset_server.reload<Image>(path);
        reloaded = reload.has_value();
        if (!reload) {
            state.import_settings_error = reload.error().message;
        }
    }
    activity.record(
        OperationSource::User,
        std::string(action),
        path.as_string() + (reloaded ? "" : ": reload failed"),
        reloaded
    );
}

Result<AssetPath, std::string>
rename_destination(const AssetPath& source, const EditorUiState& state) {
    const std::filesystem::path name(state.rename_name.data());
    if (name.empty() || name != name.filename()) {
        return failure(std::string("Rename only accepts a file name"));
    }
    auto destination = AssetPath(source.path().parent_path() / name);
    if (source.source()) {
        destination = destination.with_source(*source.source());
    }
    return destination;
}

AssetPath asset_child_path(
    const AssetPath& directory,
    const std::filesystem::path& child
) {
    auto path = AssetPath(directory.path() / child);
    if (directory.source()) {
        path = path.with_source(*directory.source());
    }
    return path;
}

Result<AssetPath, std::string> unique_copy_destination(
    const AssetPath& directory,
    const AssetPath& source,
    const AssetDatabase& database
) {
    const auto stem = source.path().stem().string();
    const auto extension = source.path().extension().string();
    for (std::size_t index = 1; index < 10'000; ++index) {
        const auto suffix = index == 1 ? std::string(" copy") :
                                         " copy " + std::to_string(index);
        auto filename = stem;
        filename += suffix;
        filename += extension;
        auto candidate = asset_child_path(directory, filename);
        auto candidate_file = database.resolve(candidate);
        if (!candidate_file) {
            return failure(std::move(candidate_file.error()));
        }
        std::error_code error;
        const bool file_exists =
            std::filesystem::exists(*candidate_file, error);
        if (error) {
            return failure(
                "Failed to inspect copy destination: " + error.message()
            );
        }
        const bool metadata_exists =
            std::filesystem::exists(database.metadata_path(candidate), error);
        if (error) {
            return failure(
                "Failed to inspect copy metadata: " + error.message()
            );
        }
        if (!file_exists && !metadata_exists && !database.metadata(candidate)) {
            return candidate;
        }
    }
    return failure(std::string("Could not find an available copy name"));
}

void reveal_asset(
    const AssetPath& path,
    AssetBrowser& browser,
    Selection& selection,
    EditorUiState& state
) {
    selection.entity = nullopt;
    selection.asset = path;
    state.reveal_asset_path = path;
    auto directory = AssetPath(path.path().parent_path());
    if (path.source()) {
        directory = directory.with_source(*path.source());
    }
    browser.navigate_to(directory);
    browser.request_refresh();
}

void paste_asset(
    const AssetPath& directory,
    AssetBrowser& browser,
    Selection& selection,
    AssetServer& asset_server,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity,
    EditorUiState& state
) {
    if (!state.asset_clipboard) {
        return;
    }
    const auto clipboard = *state.asset_clipboard;
    state.asset_operation_error = nullopt;

    if (clipboard.operation == AssetClipboardOperation::Cut) {
        const auto destination =
            asset_child_path(directory, clipboard.source.path().filename());
        auto result = database.move_asset(clipboard.source, destination);
        if (!result) {
            state.asset_operation_error = result.error();
            activity.record(
                OperationSource::User,
                "PasteAsset",
                clipboard.source.as_string() + ": " + result.error(),
                false
            );
            return;
        }
        asset_server.remap_path(result->source, result->destination);
        state.asset_clipboard = nullopt;
        state.import_settings_path = nullopt;
        reveal_asset(result->destination, browser, selection, state);
        activity.record(
            OperationSource::User,
            "PasteAsset",
            result->source.as_string() + " -> " +
                result->destination.as_string()
        );
        return;
    }

    auto destination =
        unique_copy_destination(directory, clipboard.source, database);
    if (!destination) {
        state.asset_operation_error = destination.error();
        activity.record(
            OperationSource::User,
            "PasteAssetCopy",
            clipboard.source.as_string() + ": " + destination.error(),
            false
        );
        return;
    }

    const auto* metadata = database.metadata(clipboard.source);
    const auto* importer = metadata ?
                               importers.find(metadata->importer) :
                               importers.find_for(clipboard.source.path());
    if (importer) {
        auto source_file = database.resolve(clipboard.source);
        if (!source_file) {
            state.asset_operation_error = source_file.error();
        } else {
            auto settings = metadata ? metadata->settings :
                                       importer->default_settings(*destination);
            auto result = import_asset(
                AssetImportRequest {
                    .source_file = *source_file,
                    .destination = *destination,
                    .settings = std::move(settings),
                },
                importers,
                database
            );
            if (!result) {
                state.asset_operation_error = result.error().message;
            }
        }
    } else {
        auto result = database.copy_asset_file(clipboard.source, *destination);
        if (!result) {
            state.asset_operation_error = result.error();
        }
    }
    if (state.asset_operation_error) {
        activity.record(
            OperationSource::User,
            "PasteAssetCopy",
            clipboard.source.as_string() + ": " + *state.asset_operation_error,
            false
        );
        return;
    }

    reveal_asset(*destination, browser, selection, state);
    activity.record(
        OperationSource::User,
        "PasteAssetCopy",
        clipboard.source.as_string() + " -> " + destination->as_string()
    );
}

void draw_create_folder_popup(
    AssetBrowser& browser,
    AssetDatabase& database,
    ActivityLog& activity,
    EditorUiState& state
) {
    if (!ImGui::BeginPopupModal(
            "Create Folder",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::InputText(
            "Name",
            state.create_folder_name.data(),
            state.create_folder_name.size()
        )) {
        state.asset_operation_error = nullopt;
    }
    if (state.asset_operation_error) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            state.asset_operation_error->c_str()
        );
    }

    ImGui::BeginDisabled(state.create_folder_name.front() == '\0');
    if (ImGui::Button("Create") && state.create_folder_parent) {
        const std::filesystem::path name(state.create_folder_name.data());
        if (name.empty() || name != name.filename()) {
            state.asset_operation_error =
                std::string("Folder name cannot contain a path");
        } else {
            const auto path =
                asset_child_path(*state.create_folder_parent, name);
            auto result = database.create_directory(path);
            if (!result) {
                state.asset_operation_error = result.error();
                activity.record(
                    OperationSource::User,
                    "CreateAssetFolder",
                    path.as_string() + ": " + result.error(),
                    false
                );
            } else {
                browser.request_refresh();
                state.create_folder_parent = nullopt;
                state.asset_operation_error = nullopt;
                activity.record(
                    OperationSource::User,
                    "CreateAssetFolder",
                    path.as_string()
                );
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.create_folder_parent = nullopt;
        state.asset_operation_error = nullopt;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_rename_asset_popup(
    AssetBrowser& browser,
    Selection& selection,
    AssetServer& asset_server,
    AssetDatabase& database,
    ActivityLog& activity,
    EditorUiState& state
) {
    if (!ImGui::BeginPopupModal(
            "Rename Asset",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }
    if (!state.rename_source) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const auto source = *state.rename_source;
    ImGui::TextWrapped("Source: %s", source.as_string().c_str());
    ImGui::SetNextItemWidth(560.0f);
    if (ImGui::InputTextWithHint(
            "Name",
            "renamed.png",
            state.rename_name.data(),
            state.rename_name.size()
        )) {
        state.rename_error = nullopt;
    }
    ImGui::TextDisabled(
        "The file and .meta sidecar move together; UUID and imported "
        "artifacts stay unchanged."
    );
    if (state.rename_error) {
        ImGui::PushTextWrapPos(580.0f);
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            state.rename_error->c_str()
        );
        ImGui::PopTextWrapPos();
    }

    ImGui::BeginDisabled(state.rename_name.front() == '\0');
    if (ImGui::Button("Rename")) {
        auto destination = rename_destination(source, state);
        if (!destination) {
            state.rename_error = destination.error();
            activity.record(
                OperationSource::User,
                "RenameAsset",
                source.as_string() + ": " + destination.error(),
                false
            );
        } else {
            auto result = database.move_asset(source, *destination);
            if (!result) {
                state.rename_error = result.error();
                activity.record(
                    OperationSource::User,
                    "RenameAsset",
                    source.as_string() + ": " + result.error(),
                    false
                );
            } else {
                asset_server.remap_path(result->source, result->destination);
                selection.entity = nullopt;
                selection.asset = result->destination;
                state.reveal_asset_path = result->destination;
                state.rename_source = nullopt;
                state.rename_error = nullopt;
                state.import_settings_path = nullopt;

                auto destination_directory =
                    AssetPath(result->destination.path().parent_path());
                if (result->destination.source()) {
                    destination_directory = destination_directory.with_source(
                        *result->destination.source()
                    );
                }
                browser.navigate_to(destination_directory);
                browser.request_refresh();
                activity.record(
                    OperationSource::User,
                    "RenameAsset",
                    result->source.as_string() + " -> " +
                        result->destination.as_string()
                );
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.rename_source = nullopt;
        state.rename_error = nullopt;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_delete_asset_popup(
    AssetBrowser& browser,
    Selection& selection,
    AssetServer& asset_server,
    AssetDatabase& database,
    ActivityLog& activity,
    ImGuiImages& images,
    EditorUiState& state
) {
    if (!ImGui::BeginPopupModal(
            "Delete Asset",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }
    if (!state.delete_target) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const auto target = *state.delete_target;
    ImGui::TextWrapped("Delete %s?", target.as_string().c_str());
    if (state.delete_target_is_directory) {
        ImGui::TextDisabled("Only empty folders can be deleted.");
    } else if (const auto* metadata = database.metadata(target)) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.65f, 0.25f, 1.0f},
            "References to UUID %s will become unresolved.",
            metadata->id.as_string().c_str()
        );
    } else {
        ImGui::TextDisabled("This file has no imported asset metadata.");
    }
    if (state.delete_error) {
        ImGui::PushTextWrapPos(520.0f);
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            state.delete_error->c_str()
        );
        ImGui::PopTextWrapPos();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.65f, 0.16f, 0.16f, 1.0f});
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4 {0.82f, 0.22f, 0.22f, 1.0f}
    );
    const bool confirm = ImGui::Button("Delete");
    ImGui::PopStyleColor(2);
    if (confirm) {
        bool deleted = false;
        if (state.delete_target_is_directory) {
            auto result = database.delete_empty_directory(target);
            deleted = result.has_value();
            if (!result) {
                state.delete_error = result.error();
            }
        } else {
            auto result = database.delete_asset(target);
            deleted = result.has_value();
            if (!result) {
                state.delete_error = result.error();
            } else {
                asset_server.remove_path(target);
                clear_asset_preview(images, state);
            }
        }

        if (deleted) {
            if (state.asset_clipboard &&
                state.asset_clipboard->source == target) {
                state.asset_clipboard = nullopt;
            }
            if (selection.asset && *selection.asset == target) {
                selection.asset = nullopt;
            }
            if (state.import_settings_path &&
                *state.import_settings_path == target) {
                state.import_settings_path = nullopt;
            }
            browser.clear_selection();
            browser.request_refresh();
            state.delete_target = nullopt;
            state.delete_error = nullopt;
            state.asset_operation_error = nullopt;
            activity.record(
                OperationSource::User,
                state.delete_target_is_directory ? "DeleteAssetFolder" :
                                                   "DeleteAsset",
                target.as_string()
            );
            ImGui::CloseCurrentPopup();
        } else {
            activity.record(
                OperationSource::User,
                state.delete_target_is_directory ? "DeleteAssetFolder" :
                                                   "DeleteAsset",
                target.as_string() + ": " + *state.delete_error,
                false
            );
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.delete_target = nullopt;
        state.delete_error = nullopt;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_assets(
    AssetBrowser& browser,
    Selection& selection,
    AssetServer& asset_server,
    const AssetImporterRegistry& importers,
    AssetDatabase& database,
    ActivityLog& activity,
    ImGuiImages& images,
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
    if (ImGui::SmallButton("Import...")) {
        state.import_error = nullopt;
        ImGui::OpenPopup("Import Asset");
    }
    ImGui::SameLine();
    draw_asset_breadcrumb(browser);
    draw_import_popup(browser, importers, database, activity, state);

    if (browser.refresh_requested()) {
        auto_import_project_assets(importers, database, activity);
        const bool refreshed = browser.refresh(asset_server);
        if (refreshed && state.reveal_asset_path) {
            const auto target = *state.reveal_asset_path;
            const auto entry =
                std::ranges::find(browser.entries(), target, &AssetEntry::path);
            if (entry != browser.entries().end()) {
                browser.select(*entry);
                selection.entity = nullopt;
                selection.asset = target;
            } else {
                selection.asset = nullopt;
            }
            state.reveal_asset_path = nullopt;
        } else if (!state.reveal_asset_path && !browser.selection()) {
            selection.asset = nullopt;
        }
    }
    if (browser.error()) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            browser.error()->c_str()
        );
    }
    if (state.asset_operation_error) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            state.asset_operation_error->c_str()
        );
    }

    bool navigated = false;
    bool open_rename_popup = false;
    bool open_create_folder_popup = false;
    bool open_import_popup = false;
    bool open_delete_popup = false;
    const auto table_height =
        std::max(ImGui::GetContentRegionAvail().y, 120.0f);
    if (ImGui::BeginTable(
            "asset_entries",
            4,
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
        ImGui::TableSetupColumn(
            "Status",
            ImGuiTableColumnFlags_WidthFixed,
            90.0f
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
                selection.entity = nullopt;
                selection.asset = entry.kind == AssetEntryKind::File ?
                                      Optional<AssetPath> {entry.path} :
                                      nullopt;
                if (entry.kind == AssetEntryKind::Directory &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    navigated |= browser.open(entry);
                }
            }
            if (ImGui::BeginPopupContextItem("AssetContext")) {
                browser.select(entry);
                selection.entity = nullopt;
                selection.asset = entry.kind == AssetEntryKind::File ?
                                      Optional<AssetPath> {entry.path} :
                                      nullopt;
                if (entry.kind == AssetEntryKind::Directory) {
                    if (ImGui::MenuItem("Open")) {
                        navigated |= browser.open(entry);
                    }
                    if (ImGui::MenuItem(
                            "Paste Into",
                            nullptr,
                            false,
                            state.asset_clipboard.has_value()
                        )) {
                        paste_asset(
                            entry.path,
                            browser,
                            selection,
                            asset_server,
                            importers,
                            database,
                            activity,
                            state
                        );
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete")) {
                        state.delete_target = entry.path;
                        state.delete_target_is_directory = true;
                        state.delete_error = nullopt;
                        open_delete_popup = true;
                    }
                } else {
                    const auto* metadata = database.metadata(entry.path);
                    const auto* importer =
                        metadata ? importers.find(metadata->importer) :
                                   importers.find_for(entry.path.path());
                    if (ImGui::MenuItem(
                            metadata ? "Force Reimport" : "Import",
                            nullptr,
                            false,
                            importer != nullptr
                        )) {
                        reimport_asset(
                            entry.path,
                            browser,
                            asset_server,
                            importers,
                            database,
                            activity,
                            state,
                            metadata ? "ForceReimportAsset" : "ImportAsset",
                            false
                        );
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Cut")) {
                        state.asset_clipboard = AssetClipboard {
                            .operation = AssetClipboardOperation::Cut,
                            .source = entry.path,
                        };
                        state.asset_operation_error = nullopt;
                        activity.record(
                            OperationSource::User,
                            "CutAsset",
                            entry.path.as_string()
                        );
                    }
                    if (ImGui::MenuItem("Copy")) {
                        state.asset_clipboard = AssetClipboard {
                            .operation = AssetClipboardOperation::Copy,
                            .source = entry.path,
                        };
                        state.asset_operation_error = nullopt;
                        activity.record(
                            OperationSource::User,
                            "CopyAsset",
                            entry.path.as_string()
                        );
                    }
                    if (ImGui::MenuItem("Rename...")) {
                        state.rename_source = entry.path;
                        set_text_buffer(
                            state.rename_name,
                            entry.path.path().filename().string()
                        );
                        state.rename_error = nullopt;
                        open_rename_popup = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete")) {
                        state.delete_target = entry.path;
                        state.delete_target_is_directory = false;
                        state.delete_error = nullopt;
                        open_delete_popup = true;
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(asset_type_label(entry).c_str());
            ImGui::TableSetColumnIndex(2);
            if (entry.kind == AssetEntryKind::File) {
                ImGui::TextUnformatted(asset_size_label(entry.size).c_str());
            }
            ImGui::TableSetColumnIndex(3);
            if (entry.kind == AssetEntryKind::File) {
                ImGui::TextUnformatted(
                    asset_import_state_label(database.state(entry.path))
                );
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !ImGui::IsAnyItemHovered()) {
        ImGui::OpenPopup("AssetsBackgroundContext");
    }
    if (ImGui::BeginPopup("AssetsBackgroundContext")) {
        if (ImGui::MenuItem("Create Folder...")) {
            state.create_folder_parent = browser.current_directory();
            set_text_buffer(state.create_folder_name, "New Folder");
            state.asset_operation_error = nullopt;
            open_create_folder_popup = true;
        }
        if (ImGui::MenuItem(
                "Paste",
                nullptr,
                false,
                state.asset_clipboard.has_value()
            )) {
            paste_asset(
                browser.current_directory(),
                browser,
                selection,
                asset_server,
                importers,
                database,
                activity,
                state
            );
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import...")) {
            state.import_error = nullopt;
            open_import_popup = true;
        }
        if (ImGui::MenuItem("Refresh")) {
            browser.request_refresh();
        }
        ImGui::EndPopup();
    }
    if (open_create_folder_popup) {
        ImGui::OpenPopup("Create Folder");
    }
    if (open_import_popup) {
        ImGui::OpenPopup("Import Asset");
    }
    if (open_rename_popup) {
        ImGui::OpenPopup("Rename Asset");
    }
    if (open_delete_popup) {
        ImGui::OpenPopup("Delete Asset");
    }
    draw_create_folder_popup(browser, database, activity, state);
    draw_rename_asset_popup(
        browser,
        selection,
        asset_server,
        database,
        activity,
        state
    );
    draw_delete_asset_popup(
        browser,
        selection,
        asset_server,
        database,
        activity,
        images,
        state
    );
    if (navigated && browser.refresh_requested()) {
        auto_import_project_assets(importers, database, activity);
        browser.refresh(asset_server);
        selection.asset = nullopt;
    }

    ImGui::End();
}

void draw_scene(EditorUiState& state, const EditorSceneSession& session) {
    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    if (session.external_change_pending) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.7f, 0.2f, 1.0f},
            "Scene changed on disk; use File > Reload Scene from Disk or Keep "
            "Local Scene."
        );
    }
    if (session.error) {
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            session.error->c_str()
        );
    }
    const auto available = ImGui::GetContentRegionAvail();
    const auto width = static_cast<uint32>(std::max(available.x, 1.0f));
    const auto height = static_cast<uint32>(std::max(available.y, 1.0f));
    state.scene_width = width;
    state.scene_height = height;

    if (!state.scene_texture) {
        ImGui::TextDisabled("Waiting for the 2D render target...");
    } else {
        ImGui::Image(
            state.scene_texture.texture_id(),
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
    auto& asset_database = world.resource<AssetDatabase>();
    auto& asset_watcher = world.resource<ProjectAssetWatcher>();
    auto& scene_session = world.resource<EditorSceneSession>();
    const auto& asset_importers =
        static_cast<const World&>(world).resource<AssetImporterRegistry>();
    const auto& images =
        static_cast<const World&>(world).resource<Assets<Image>>();
    auto& image_textures = world.resource<ImGuiImages>();
    const auto& operations =
        static_cast<const World&>(world).resource<ComponentOperations>();
    const auto& agent =
        static_cast<const World&>(world).resource<ExternalAgentStatus>();
    const auto activity_sequence = activity.entries().empty() ?
                                       std::uint64_t {0} :
                                       activity.entries().back().sequence;

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
        draw_hierarchy(world, selection, activity, scene_session);
    }
    if (state.show_inspector) {
        draw_inspector(
            world,
            selection,
            operations,
            activity,
            asset_browser,
            asset_server,
            asset_importers,
            asset_database,
            images,
            image_textures,
            state
        );
    }
    if (state.show_scene) {
        draw_scene(state, scene_session);
    }
    if (state.show_assets) {
        draw_assets(
            asset_browser,
            selection,
            asset_server,
            asset_importers,
            asset_database,
            activity,
            image_textures,
            state
        );
    }
    if (state.show_activity) {
        draw_activity(activity, agent);
    }

    const bool changed_assets = std::ranges::any_of(
        activity.entries(),
        [activity_sequence](const OperationEntry& entry) {
            return entry.sequence > activity_sequence &&
                   entry.source != OperationSource::ExternalAgent &&
                   (entry.action.contains("Asset") ||
                    entry.action.contains("Import"));
        }
    );
    if (changed_assets) {
        if (auto status = asset_watcher.acknowledge(); !status) {
            activity.record(
                OperationSource::Editor,
                "WatchProjectAssets",
                status.error(),
                false
            );
        }
    }
    const bool changed_scene = std::ranges::any_of(
        activity.entries(),
        [activity_sequence](const OperationEntry& entry) {
            return entry.sequence > activity_sequence &&
                   entry.source == OperationSource::User &&
                   (entry.action == "CreateEntity" ||
                    entry.action == "DeleteEntity" ||
                    entry.action == "AddComponent" ||
                    entry.action == "RemoveComponent" ||
                    entry.action == "SetComponentProperty" ||
                    entry.action == "ReparentEntity");
        }
    );
    if (changed_scene) {
        scene_session.dirty = true;
    }
}

void setup_welcome_scene(
    ResRW<AssetServer> assets,
    ResRW<Selection> selection,
    ResRW<ActivityLog> activity,
    ResRW<EditorSceneSession> scene,
    Commands commands
) {
    const auto camera = commands.spawn().add(
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
    scene->bindings.ensure(camera.id());
    scene->bindings.ensure(root.id());
    scene->bindings.ensure(sprite.id());
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
    if (!app.has_resource<AssetDatabase>() ||
        !app.has_resource<AssetImporterRegistry>()) {
        fatal("EditorPlugin requires AssetsPlugin to be installed first");
    }
    auto& render_app = app.sub_app<RenderApp>();
    if (!render_app.has_resource<SpriteOutput>() ||
        static_cast<const SubApp&>(render_app).resource<SpriteOutput>().mode !=
            SpriteOutputMode::Texture) {
        fatal("EditorPlugin requires SpritePlugin texture output mode");
    }
    if (!app.has_resource<ImGuiImages>() ||
        !app.has_resource<ImGuiRenderTextures>()) {
        fatal("EditorPlugin requires ImGui texture services");
    }

    app.add_plugin<AssetPlugin<SceneDocument, SceneDocumentLoader>>();

    ProjectAssetWatcher asset_watcher(app.resource<AssetDatabase>().root());
    if (auto status = asset_watcher.acknowledge(); !status) {
        warn("Failed to initialize project asset watcher: {}", status.error());
    }

    const auto scene_texture =
        app.resource<ImGuiRenderTextures>().reserve_texture();
    app.add_resource(ComponentOperations {})
        .add_resource(ActivityLog {})
        .add_resource(AssetBrowser {})
        .add_resource(std::move(asset_watcher))
        .add_resource(AssetWatchSyncState {})
        .add_resource(EditorSceneSession {})
        .add_resource(ExternalAgentStatus {})
        .add_resource(Selection {})
        .add_resource(EditorUiState {.scene_texture = scene_texture});

    render_app.add_resource(ExtractedEditorViewport {})
        .add_shutdown(shutdown_editor_viewport)
        .configure_sets(
            RenderUpdate,
            EditorRenderSystems::BindViewport {}
                .after<SpriteSystems::PrepareOutput>()
        )
        .add_systems(RenderExtract, extract_editor_viewport)
        .add_systems(
            RenderUpdate,
            bind_editor_viewport |
                in_set<RenderingSystems::PrepareResources>() |
                in_set<EditorRenderSystems::BindViewport>()
        );

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

    bool loaded_main_scene = false;
    const auto& project = app.resource<Project>();
    if (project.config().main_scene) {
        auto scene_path =
            app.resource<AssetServer>().resolve(*project.config().main_scene);
        if (!scene_path) {
            app.resource<ActivityLog>().record(
                OperationSource::Editor,
                "OpenMainScene",
                scene_path.error().message,
                false
            );
        } else {
            auto metadata =
                app.resource<AssetDatabase>().ensure_native_asset(*scene_path);
            auto document =
                read_scene_document(*scene_path, app.resource<AssetDatabase>());
            if (!metadata || !document) {
                const auto message =
                    !metadata ? metadata.error() : document.error();
                app.resource<ActivityLog>().record(
                    OperationSource::Editor,
                    "OpenMainScene",
                    message,
                    false
                );
            } else {
                auto status = replace_editor_scene(
                    app.world(),
                    *scene_path,
                    std::move(*document),
                    app.resource<ComponentOperations>(),
                    app.resource<Selection>(),
                    app.resource<ActivityLog>(),
                    app.resource<EditorSceneSession>(),
                    OperationSource::Editor
                );
                if (!status) {
                    app.resource<ActivityLog>().record(
                        OperationSource::Editor,
                        "OpenMainScene",
                        status.error(),
                        false
                    );
                } else {
                    loaded_main_scene = true;
                    app.resource<ActivityLog>().record(
                        OperationSource::Editor,
                        "OpenMainScene",
                        scene_path->as_string()
                    );
                }
            }
        }
    }

    auto import_report = import_pending_assets(
        app.resource<AssetImporterRegistry>(),
        app.resource<AssetDatabase>()
    );
    if (!import_report) {
        warn("Failed to import project assets: {}", import_report.error());
        app.resource<ActivityLog>().record(
            OperationSource::Editor,
            "ImportProjectAssets",
            import_report.error(),
            false
        );
    } else if (
        !import_report->imported.empty() || !import_report->failed.empty()
    ) {
        app.resource<ActivityLog>().record(
            OperationSource::Editor,
            "ImportProjectAssets",
            std::format(
                "{} imported, {} failed",
                import_report->imported.size(),
                import_report->failed.size()
            ),
            import_report->failed.empty()
        );
    }
    if (auto status = app.resource<ProjectAssetWatcher>().acknowledge();
        !status) {
        warn("Failed to synchronize project asset watcher: {}", status.error());
    }

    if (m_config.create_welcome_scene && !loaded_main_scene) {
        app.add_systems(PreStartUp, setup_welcome_scene);
    }
    app.add_systems(Update, sync_project_assets | main_thread());
    app.add_systems(
        RenderUpdate,
        draw_editor | in_set<RenderingSystems::Render>() | main_thread()
    );
}

void EditorPlugin::cleanup(App& app) noexcept {
    if (!app.has_resource<EditorUiState>()) {
        return;
    }

    auto& state = app.resource<EditorUiState>();
    if (app.has_resource<ImGuiImages>()) {
        clear_asset_preview(app.resource<ImGuiImages>(), state);
    }
    if (state.scene_texture && app.has_resource<ImGuiRenderTextures>()) {
        app.resource<ImGuiRenderTextures>().release_texture(
            state.scene_texture
        );
        state.scene_texture = {};
    }
}

} // namespace fei::editor
