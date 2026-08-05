#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "ecs/type_tags.hpp"
#include "ecs/world.hpp"
#include "editor/activity.hpp"
#include "editor/activity_panel.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_watcher.hpp"
#include "editor/assets_panel.hpp"
#include "editor/component_operations.hpp"
#include "editor/hierarchy_panel.hpp"
#include "editor/inspector_panel.hpp"
#include "editor/layer.hpp"
#include "editor/plugin.hpp"
#include "editor/scene_panel.hpp"
#include "editor/scene_session.hpp"
#include "imgui/texture.hpp"
#include "refl/registry.hpp"
#include "scene/document.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace fei::editor {
namespace {

enum class PendingSceneAction : std::uint8_t {
    None,
    NewScene,
    OpenScene,
    ReloadScene,
    Exit,
};

struct EditorUiState {
    bool layout_initialized {false};
    bool style_initialized {false};
    PendingSceneAction pending_scene_action {PendingSceneAction::None};
    Optional<AssetPath> pending_scene_path;
    std::array<char, 512> save_as_path {};
    Optional<std::string> save_as_error;
    bool open_unsaved_changes {false};
    bool open_save_as {false};
    bool allow_save_as_overwrite {false};
};

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

template<std::size_t Size>
void set_text_buffer(std::array<char, Size>& buffer, std::string_view value) {
    const auto length = std::min(value.size(), buffer.size() - 1);
    std::copy_n(value.begin(), length, buffer.begin());
    buffer[length] = '\0';
}

AssetPath normalized_project_path(const AssetPath& path) {
    auto normalized = path.normalized();
    return normalized.source() ? normalized : normalized.with_source("project");
}

void record_scene_error(
    SceneSession& scene,
    ActivityLog& activity,
    std::string_view action,
    std::string error
) {
    scene.error = error;
    activity.record(
        OperationSource::User,
        std::string(action),
        std::move(error),
        false
    );
}

void clear_pending_scene_action(EditorUiState& state) {
    state.pending_scene_action = PendingSceneAction::None;
    state.pending_scene_path = nullopt;
}

bool perform_pending_scene_action(World& world, EditorUiState& state) {
    const auto action = state.pending_scene_action;
    const auto path = state.pending_scene_path;
    clear_pending_scene_action(state);

    auto& scene = world.resource<SceneSession>();
    auto& activity = world.resource<ActivityLog>();
    auto& selection = world.resource<Selection>();
    const auto& database =
        static_cast<const World&>(world).resource<AssetDatabase>();
    const auto& operations =
        static_cast<const World&>(world).resource<ComponentOperations>();

    switch (action) {
        case PendingSceneAction::None:
            return true;
        case PendingSceneAction::NewScene: {
            const auto new_path = next_untitled_scene_path(database);
            auto status = replace_scene(
                world,
                new_path,
                SceneDocument {},
                operations,
                selection,
                activity,
                scene,
                OperationSource::User
            );
            if (!status) {
                record_scene_error(scene, activity, "NewScene", status.error());
                return false;
            }
            scene.dirty = true;
            activity.record(
                OperationSource::User,
                "NewScene",
                new_path.as_string()
            );
            return true;
        }
        case PendingSceneAction::OpenScene: {
            if (!path) {
                return false;
            }
            auto document = read_scene_document(*path, database);
            if (!document) {
                record_scene_error(
                    scene,
                    activity,
                    "OpenScene",
                    document.error()
                );
                return false;
            }
            auto status = replace_scene(
                world,
                *path,
                std::move(*document),
                operations,
                selection,
                activity,
                scene,
                OperationSource::User
            );
            if (!status) {
                record_scene_error(
                    scene,
                    activity,
                    "OpenScene",
                    status.error()
                );
                return false;
            }
            activity
                .record(OperationSource::User, "OpenScene", path->as_string());
            return true;
        }
        case PendingSceneAction::ReloadScene: {
            auto status = reload_scene(
                world,
                database,
                operations,
                selection,
                activity,
                scene,
                OperationSource::User
            );
            if (!status) {
                record_scene_error(
                    scene,
                    activity,
                    "ReloadScene",
                    status.error()
                );
                return false;
            }
            activity.record(
                OperationSource::User,
                "ReloadScene",
                scene.path ? scene.path->as_string() : std::string {}
            );
            return true;
        }
        case PendingSceneAction::Exit:
            world.resource<AppStates>().should_stop = true;
            return true;
    }
    return false;
}

void request_scene_action(
    World& world,
    EditorUiState& state,
    PendingSceneAction action,
    Optional<AssetPath> path = nullopt
) {
    state.pending_scene_action = action;
    state.pending_scene_path = std::move(path);
    if (world.resource<SceneSession>().dirty) {
        state.open_unsaved_changes = true;
        return;
    }
    perform_pending_scene_action(world, state);
}

bool save_current_scene(World& world) {
    auto& scene = world.resource<SceneSession>();
    auto& activity = world.resource<ActivityLog>();
    auto status = save_scene(
        world,
        world.resource<AssetDatabase>(),
        world.resource<ProjectAssetWatcher>(),
        static_cast<const World&>(world).resource<ComponentOperations>(),
        activity,
        scene
    );
    if (!status) {
        record_scene_error(scene, activity, "SaveScene", status.error());
        return false;
    }
    return true;
}

void request_save_as(World& world, EditorUiState& state) {
    const auto& scene =
        static_cast<const World&>(world).resource<SceneSession>();
    const auto path =
        scene.path ?
            *scene.path :
            next_untitled_scene_path(
                static_cast<const World&>(world).resource<AssetDatabase>()
            );
    set_text_buffer(state.save_as_path, path.as_string());
    state.save_as_error = nullopt;
    state.allow_save_as_overwrite = false;
    state.open_save_as = true;
}

bool scene_path_exists(const AssetPath& path, const AssetDatabase& database) {
    auto file = database.resolve(normalized_project_path(path));
    if (!file) {
        return false;
    }
    std::error_code error;
    return std::filesystem::exists(*file, error) && !error;
}

bool save_scene_to_requested_path(
    World& world,
    EditorUiState& state,
    bool overwrite
) {
    auto& scene = world.resource<SceneSession>();
    auto& activity = world.resource<ActivityLog>();
    const AssetPath requested(state.save_as_path.data());
    auto status = save_scene_as(
        world,
        requested,
        overwrite,
        world.resource<AssetDatabase>(),
        world.resource<ProjectAssetWatcher>(),
        static_cast<const World&>(world).resource<ComponentOperations>(),
        activity,
        scene
    );
    if (status) {
        state.save_as_error = nullopt;
        state.allow_save_as_overwrite = false;
        return true;
    }

    state.save_as_error = status.error();
    scene.error = status.error();
    activity
        .record(OperationSource::User, "SaveSceneAs", status.error(), false);
    const auto normalized = normalized_project_path(requested);
    const bool same_as_current =
        scene.path && normalized == normalized_project_path(*scene.path);
    state.allow_save_as_overwrite =
        !overwrite && !same_as_current &&
        scene_path_exists(normalized, world.resource<AssetDatabase>());
    return false;
}

void draw_unsaved_changes_popup(World& world, EditorUiState& state) {
    if (state.open_unsaved_changes) {
        ImGui::OpenPopup("Unsaved Scene Changes");
        state.open_unsaved_changes = false;
    }
    if (!ImGui::BeginPopupModal(
            "Unsaved Scene Changes",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }

    const auto& scene =
        static_cast<const World&>(world).resource<SceneSession>();
    ImGui::TextUnformatted("The current scene has unsaved changes.");
    if (scene.path) {
        ImGui::TextDisabled("%s", scene.path->as_string().c_str());
    }
    ImGui::Separator();

    if (ImGui::Button("Save")) {
        if (!scene.path) {
            ImGui::CloseCurrentPopup();
            request_save_as(world, state);
        } else if (save_current_scene(world)) {
            ImGui::CloseCurrentPopup();
            perform_pending_scene_action(world, state);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        ImGui::CloseCurrentPopup();
        perform_pending_scene_action(world, state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        clear_pending_scene_action(state);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_save_as_popup(World& world, EditorUiState& state) {
    if (state.open_save_as) {
        ImGui::OpenPopup("Save Scene As");
        state.open_save_as = false;
    }
    if (!ImGui::BeginPopupModal(
            "Save Scene As",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }

    ImGui::TextUnformatted("Save the scene as a project asset.");
    ImGui::SetNextItemWidth(560.0f);
    if (ImGui::InputText(
            "Path",
            state.save_as_path.data(),
            state.save_as_path.size()
        )) {
        state.save_as_error = nullopt;
        state.allow_save_as_overwrite = false;
    }
    ImGui::TextDisabled("Expected: project://scenes/name.scene.yaml");

    if (state.save_as_error) {
        ImGui::PushTextWrapPos(580.0f);
        ImGui::TextColored(
            ImVec4 {0.95f, 0.35f, 0.35f, 1.0f},
            "%s",
            state.save_as_error->c_str()
        );
        ImGui::PopTextWrapPos();
    }

    const bool has_path = state.save_as_path.front() != '\0';
    ImGui::BeginDisabled(!has_path);
    if (ImGui::Button("Save")) {
        const auto& scene =
            static_cast<const World&>(world).resource<SceneSession>();
        const auto requested =
            normalized_project_path(AssetPath(state.save_as_path.data()));
        const bool same_as_current =
            scene.path && requested == normalized_project_path(*scene.path);
        if (save_scene_to_requested_path(world, state, same_as_current)) {
            ImGui::CloseCurrentPopup();
            perform_pending_scene_action(world, state);
        }
    }
    ImGui::EndDisabled();

    if (state.allow_save_as_overwrite) {
        ImGui::SameLine();
        if (ImGui::Button("Overwrite")) {
            if (save_scene_to_requested_path(world, state, true)) {
                ImGui::CloseCurrentPopup();
                perform_pending_scene_action(world, state);
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.save_as_error = nullopt;
        state.allow_save_as_overwrite = false;
        clear_pending_scene_action(state);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void handle_scene_shortcuts(World& world, EditorUiState& state) {
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || !io.KeyCtrl ||
        !ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        return;
    }
    if (io.KeyShift) {
        request_save_as(world, state);
        return;
    }

    const auto& scene =
        static_cast<const World&>(world).resource<SceneSession>();
    if (scene.path) {
        save_current_scene(world);
    } else {
        request_save_as(world, state);
    }
}

void draw_main_menu(
    World& world,
    EditorUiState& state,
    ScenePanel& scene_panel,
    ActivityPanel& activity_panel,
    HierarchyPanel& hierarchy_panel,
    InspectorPanel& inspector_panel,
    AssetsPanel& assets_panel
) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        const auto& scene =
            static_cast<const World&>(world).resource<SceneSession>();
        const auto& selection =
            static_cast<const World&>(world).resource<Selection>();
        if (ImGui::MenuItem("New Scene")) {
            request_scene_action(world, state, PendingSceneAction::NewScene);
        }
        const bool can_open_selected =
            selection.asset && is_scene_document_path(*selection.asset);
        if (ImGui::MenuItem(
                "Open Selected Scene",
                nullptr,
                false,
                can_open_selected
            )) {
            request_scene_action(
                world,
                state,
                PendingSceneAction::OpenScene,
                selection.asset
            );
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
            if (scene.path) {
                save_current_scene(world);
            } else {
                request_save_as(world, state);
            }
        }
        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
            request_save_as(world, state);
        }
        if (ImGui::MenuItem(
                "Reload Scene from Disk",
                nullptr,
                false,
                scene.path.has_value()
            )) {
            request_scene_action(world, state, PendingSceneAction::ReloadScene);
        }
        if (scene.external_change_pending &&
            ImGui::MenuItem("Keep Local Scene")) {
            auto& mutable_scene = world.resource<SceneSession>();
            mutable_scene.external_change_pending = false;
            mutable_scene.dirty = true;
            world.resource<ActivityLog>().record(
                OperationSource::User,
                "KeepLocalScene",
                mutable_scene.path ? mutable_scene.path->as_string() :
                                     std::string {}
            );
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            request_scene_action(world, state, PendingSceneAction::Exit);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Scene", nullptr, scene_panel.is_open())) {
            scene_panel.set_open(!scene_panel.is_open());
        }
        if (ImGui::MenuItem("Hierarchy", nullptr, hierarchy_panel.is_open())) {
            hierarchy_panel.set_open(!hierarchy_panel.is_open());
        }
        if (ImGui::MenuItem("Inspector", nullptr, inspector_panel.is_open())) {
            inspector_panel.set_open(!inspector_panel.is_open());
        }
        if (ImGui::MenuItem("Assets", nullptr, assets_panel.is_open())) {
            assets_panel.set_open(!assets_panel.is_open());
        }
        if (ImGui::MenuItem(
                "Agent Activity",
                nullptr,
                activity_panel.is_open()
            )) {
            activity_panel.set_open(!activity_panel.is_open());
        }
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

void draw_editor(
    World& world,
    EditorUiState& state,
    ScenePanel& scene_panel,
    ActivityPanel& activity_panel,
    HierarchyPanel& hierarchy_panel,
    InspectorPanel& inspector_panel,
    AssetsPanel& assets_panel
) {
    auto& viewport = world.resource<SceneViewport>();
    auto& selection = world.resource<Selection>();
    auto& activity = world.resource<ActivityLog>();
    auto& asset_browser = world.resource<AssetBrowser>();
    auto& asset_server = world.resource<AssetServer>();
    auto& asset_database = world.resource<AssetDatabase>();
    auto& asset_watcher = world.resource<ProjectAssetWatcher>();
    auto& scene_session = world.resource<SceneSession>();
    auto& app_states = world.resource<AppStates>();
    if (app_states.should_stop && scene_session.dirty &&
        state.pending_scene_action == PendingSceneAction::None) {
        app_states.should_stop = false;
        request_scene_action(world, state, PendingSceneAction::Exit);
    }
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
    const AssetsPanelContext assets_context {
        .browser = asset_browser,
        .selection = selection,
        .asset_server = asset_server,
        .importers = asset_importers,
        .database = asset_database,
        .activity = activity,
        .images = images,
        .image_textures = image_textures,
    };

    initialize_style(state);
    draw_main_menu(
        world,
        state,
        scene_panel,
        activity_panel,
        hierarchy_panel,
        inspector_panel,
        assets_panel
    );

    const ImGuiID dockspace = ImGui::GetID("FeiEditorDockspace");
    build_default_layout(dockspace, state);
    ImGui::DockSpaceOverViewport(
        dockspace,
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode
    );

    handle_scene_shortcuts(world, state);
    draw_unsaved_changes_popup(world, state);
    draw_save_as_popup(world, state);

    hierarchy_panel.draw(
        HierarchyPanelContext {
            .world = world,
            .selection = selection,
            .activity = activity,
            .bindings = scene_session.bindings,
        }
    );
    inspector_panel.draw(
        InspectorPanelContext {
            .world = world,
            .selection = selection,
            .operations = operations,
            .activity = activity,
            .draw_asset = [&](const AssetPath& path) {
                assets_panel.draw_inspector(path, assets_context);
            },
        }
    );
    scene_panel.draw(
        viewport,
        scene_session.external_change_pending,
        scene_session.error ? std::string_view(*scene_session.error) :
                              std::string_view {}
    );
    assets_panel.draw(assets_context);
    activity_panel.draw(
        activity,
        agent,
        Registry::instance().types_with_tag(ComponentTypeTag).size()
    );

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

} // namespace

struct EditorLayer::Impl {
    EditorUiState state;
    ScenePanel scene_panel;
    ActivityPanel activity_panel;
    HierarchyPanel hierarchy_panel;
    InspectorPanel inspector_panel;
    AssetsPanel assets_panel;
};

EditorLayer::EditorLayer() : m_impl(std::make_unique<Impl>()) {}

EditorLayer::~EditorLayer() = default;

void EditorLayer::draw(World& world) {
    draw_editor(
        world,
        m_impl->state,
        m_impl->scene_panel,
        m_impl->activity_panel,
        m_impl->hierarchy_panel,
        m_impl->inspector_panel,
        m_impl->assets_panel
    );
}

void EditorLayer::shutdown(World& world) noexcept {
    if (world.has_resource<ImGuiImages>()) {
        m_impl->assets_panel.shutdown(world.resource<ImGuiImages>());
    }
}

} // namespace fei::editor
