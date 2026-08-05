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
#include <cstdint>
#include <imgui.h>
#include <imgui_internal.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace fei::editor {
namespace {

struct EditorUiState {
    bool layout_initialized {false};
    bool style_initialized {false};
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

void draw_main_menu(
    World& world,
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
        auto& scene = world.resource<SceneSession>();
        auto& activity = world.resource<ActivityLog>();
        auto& selection = world.resource<Selection>();
        if (ImGui::MenuItem("New Scene", nullptr, false, !scene.dirty)) {
            const auto path = next_untitled_scene_path(
                static_cast<const World&>(world).resource<AssetDatabase>()
            );
            auto status = replace_scene(
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
                document ? replace_scene(
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
            auto status = save_scene(
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
            auto status = reload_scene(
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
