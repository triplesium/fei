#include "editor/project_asset_sync.hpp"

#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "ecs/world.hpp"
#include "editor/activity.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_watcher.hpp"
#include "editor/component_operations.hpp"
#include "editor/plugin.hpp"
#include "editor/scene_session.hpp"

#include <unordered_map>
#include <unordered_set>

namespace fei::editor {

void ProjectAssetSynchronizer::update(World& editor_world) {
    auto* watcher = &editor_world.resource<ProjectAssetWatcher>();
    auto* database = &editor_world.resource<AssetDatabase>();
    const auto* importers = &static_cast<const World&>(editor_world)
                                 .resource<AssetImporterRegistry>();
    auto* asset_server = &editor_world.resource<AssetServer>();
    auto* browser = &editor_world.resource<AssetBrowser>();
    auto* activity = &editor_world.resource<ActivityLog>();
    auto* scene_session = &editor_world.resource<SceneSession>();
    const auto* operations = &static_cast<const World&>(editor_world)
                                  .resource<ComponentOperations>();
    auto* selection = &editor_world.resource<Selection>();
    auto* world = &editor_world;

    auto changes = watcher->poll();
    if (!changes) {
        if (!m_last_error || *m_last_error != changes.error()) {
            activity->record(
                OperationSource::Editor,
                "WatchProjectAssets",
                changes.error(),
                false
            );
            m_last_error = changes.error();
        }
        return;
    }
    m_last_error = nullopt;
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
        m_last_error = acknowledge.error();
    }
}

} // namespace fei::editor
