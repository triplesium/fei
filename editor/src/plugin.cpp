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
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
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
#include "editor/scene_panel.hpp"
#include "editor/scene_session.hpp"
#include "imgui/plugin.hpp"
#include "imgui/renderer.hpp"
#include "imgui/texture.hpp"
#include "project/project.hpp"
#include "refl/registry.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "scene/document.hpp"
#include "sprite/components.hpp"
#include "sprite/output.hpp"
#include "sprite/plugin.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
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

struct EditorUiState {
    bool layout_initialized {false};
    bool style_initialized {false};
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
    Extract<ResRO<SceneViewport>> editor,
    ResRW<ExtractedEditorViewport> viewport,
    ResRW<SpriteOutput> output
) {
    viewport->texture = (*editor)->texture;
    viewport->width = (*editor)->visible ? (*editor)->width : 0;
    viewport->height = (*editor)->visible ? (*editor)->height : 0;
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

void sync_project_assets(
    ResRW<ProjectAssetWatcher> watcher,
    ResRW<AssetDatabase> database,
    Res<AssetImporterRegistry> importers,
    ResRW<AssetServer> asset_server,
    ResRW<AssetBrowser> browser,
    ResRW<ActivityLog> activity,
    ResRW<AssetWatchSyncState> sync_state,
    ResRW<SceneSession> scene_session,
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
            auto status = reload_scene(
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

void setup_welcome_scene(
    ResRW<AssetServer> assets,
    ResRW<Selection> selection,
    ResRW<ActivityLog> activity,
    ResRW<SceneSession> scene,
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
        .add_resource(SceneSession {})
        .add_resource(ExternalAgentStatus {})
        .add_resource(Selection {})
        .add_resource(SceneViewport {.texture = scene_texture});

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
                auto status = replace_scene(
                    app.world(),
                    *scene_path,
                    std::move(*document),
                    app.resource<ComponentOperations>(),
                    app.resource<Selection>(),
                    app.resource<ActivityLog>(),
                    app.resource<SceneSession>(),
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
}

void EditorPlugin::cleanup(App& app) noexcept {
    if (!app.has_resource<SceneViewport>()) {
        return;
    }

    auto& viewport = app.resource<SceneViewport>();
    if (viewport.texture && app.has_resource<ImGuiRenderTextures>()) {
        app.resource<ImGuiRenderTextures>().release_texture(viewport.texture);
        viewport.texture = {};
    }
}

} // namespace fei::editor
