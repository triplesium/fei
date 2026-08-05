#include "editor/project_session.hpp"

#include "app/app.hpp"
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
#include "ecs/system_params.hpp"
#include "editor/activity.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_watcher.hpp"
#include "editor/component_operations.hpp"
#include "editor/plugin.hpp"
#include "editor/project_asset_sync.hpp"
#include "editor/scene_session.hpp"
#include "project/project.hpp"
#include "scene/document.hpp"
#include "sprite/components.hpp"

#include <format>
#include <utility>

namespace fei::editor {
namespace {

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

bool load_main_scene(App& app) {
    const auto& project = app.resource<Project>();
    if (!project.config().main_scene) {
        return false;
    }

    auto scene_path =
        app.resource<AssetServer>().resolve(*project.config().main_scene);
    if (!scene_path) {
        app.resource<ActivityLog>().record(
            OperationSource::Editor,
            "OpenMainScene",
            scene_path.error().message,
            false
        );
        return false;
    }

    auto metadata =
        app.resource<AssetDatabase>().ensure_native_asset(*scene_path);
    auto document =
        read_scene_document(*scene_path, app.resource<AssetDatabase>());
    if (!metadata || !document) {
        const auto message = !metadata ? metadata.error() : document.error();
        app.resource<ActivityLog>()
            .record(OperationSource::Editor, "OpenMainScene", message, false);
        return false;
    }

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
        return false;
    }

    app.resource<ActivityLog>().record(
        OperationSource::Editor,
        "OpenMainScene",
        scene_path->as_string()
    );
    return true;
}

void import_project_assets(App& app) {
    auto report = import_pending_assets(
        app.resource<AssetImporterRegistry>(),
        app.resource<AssetDatabase>()
    );
    if (!report) {
        warn("Failed to import project assets: {}", report.error());
        app.resource<ActivityLog>().record(
            OperationSource::Editor,
            "ImportProjectAssets",
            report.error(),
            false
        );
        return;
    }
    if (report->imported.empty() && report->failed.empty()) {
        return;
    }

    app.resource<ActivityLog>().record(
        OperationSource::Editor,
        "ImportProjectAssets",
        std::format(
            "{} imported, {} failed",
            report->imported.size(),
            report->failed.size()
        ),
        report->failed.empty()
    );
}

} // namespace

Status<std::string>
EditorProjectSession::open(App& app, bool create_welcome_scene) {
    if (m_open) {
        return failure(std::string("The editor project is already open"));
    }
    if (!app.has_resource<Project>()) {
        return failure(std::string("Project resource is not available"));
    }

    app.add_plugin<AssetPlugin<SceneDocument, SceneDocumentLoader>>();

    ProjectAssetWatcher asset_watcher(app.resource<AssetDatabase>().root());
    if (auto status = asset_watcher.acknowledge(); !status) {
        warn("Failed to initialize project asset watcher: {}", status.error());
    }

    app.add_resource(ComponentOperations {})
        .add_resource(ActivityLog {})
        .add_resource(AssetBrowser {})
        .add_resource(std::move(asset_watcher))
        .add_resource(ProjectAssetSynchronizer {})
        .add_resource(SceneSession {})
        .add_resource(ExternalAgentStatus {})
        .add_resource(Selection {});

    auto& operations = app.resource<ComponentOperations>();
    if (!register_asset_handle_codec<Image>(
            operations.codecs(),
            app.resource<AssetServer>(),
            app.resource<Assets<Image>>()
        )) {
        return failure(
            std::string("Failed to register the Image handle codec")
        );
    }

    app.resource<ActivityLog>().record(
        OperationSource::Editor,
        "StartEditor",
        "External-agent mode; no internal agent"
    );

    const bool loaded_main_scene = load_main_scene(app);
    import_project_assets(app);

    if (auto status = app.resource<ProjectAssetWatcher>().acknowledge();
        !status) {
        warn("Failed to synchronize project asset watcher: {}", status.error());
    }

    if (create_welcome_scene && !loaded_main_scene) {
        app.add_systems(PreStartUp, setup_welcome_scene);
    }

    m_open = true;
    return {};
}

} // namespace fei::editor
