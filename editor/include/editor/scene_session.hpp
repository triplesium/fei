#pragma once

#include "asset/path.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "scene/document.hpp"

#include <string>

namespace fei {

class AssetDatabase;
class World;

namespace editor {

class ActivityLog;
class ComponentOperations;
class ProjectAssetWatcher;
enum class OperationSource;
struct Selection;

struct SceneSession {
    Optional<AssetPath> path;
    Optional<SceneDocument> document;
    SceneEntityBindings bindings;
    bool dirty {false};
    bool external_change_pending {false};
    Optional<std::string> error;
};

[[nodiscard]] bool is_scene_document_path(const AssetPath& path);

[[nodiscard]] Result<SceneDocument, std::string>
read_scene_document(const AssetPath& path, const AssetDatabase& database);

Status<std::string> replace_scene(
    World& world,
    const AssetPath& path,
    SceneDocument document,
    const ComponentOperations& operations,
    Selection& selection,
    ActivityLog& activity,
    SceneSession& session,
    OperationSource source
);

Status<std::string> save_scene(
    World& world,
    AssetDatabase& database,
    ProjectAssetWatcher& watcher,
    const ComponentOperations& operations,
    ActivityLog& activity,
    SceneSession& session
);

[[nodiscard]] AssetPath next_untitled_scene_path(const AssetDatabase& database);

Status<std::string> reload_scene(
    World& world,
    const AssetDatabase& database,
    const ComponentOperations& operations,
    Selection& selection,
    ActivityLog& activity,
    SceneSession& session,
    OperationSource source
);

} // namespace editor
} // namespace fei
