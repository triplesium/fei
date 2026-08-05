#include "editor/scene_session.hpp"

#include "asset/database.hpp"
#include "asset/io.hpp"
#include "ecs/world.hpp"
#include "editor/activity.hpp"
#include "editor/asset_watcher.hpp"
#include "editor/component_operations.hpp"
#include "editor/plugin.hpp"
#include "serialization/json_archive.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace fei::editor {

bool is_scene_document_path(const AssetPath& path) {
    return path.path().filename().string().ends_with(".scene.yaml");
}

static Status<std::string> write_file_atomically(
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

static bool serialized_nodes_equal(
    const serialization::SerializedNode& lhs,
    const serialization::SerializedNode& rhs
) {
    const auto lhs_json = serialization::write_json(lhs, 0);
    const auto rhs_json = serialization::write_json(rhs, 0);
    return lhs_json && rhs_json && *lhs_json == *rhs_json;
}

static void record_scene_diff(
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

Status<std::string> replace_scene(
    World& world,
    const AssetPath& path,
    SceneDocument document,
    const ComponentOperations& operations,
    Selection& selection,
    ActivityLog& activity,
    SceneSession& session,
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

static Status<std::string> save_scene_to(
    World& world,
    const AssetPath& requested_path,
    bool overwrite,
    AssetDatabase& database,
    ProjectAssetWatcher& watcher,
    const ComponentOperations& operations,
    ActivityLog& activity,
    SceneSession& session
) {
    auto path = requested_path.normalized();
    if (!path.source()) {
        path = path.with_source("project");
    }
    if (*path.source() != "project" || path.is_unapproved() ||
        path.path().empty()) {
        return failure(
            "Scene path must be a safe project:// path: " +
            requested_path.as_string()
        );
    }
    if (!is_scene_document_path(path)) {
        return failure(
            "Scene path must end with .scene.yaml: " + path.as_string()
        );
    }

    auto file = database.resolve(path);
    if (!file) {
        return failure(std::move(file.error()));
    }
    std::error_code existence_error;
    const bool exists = std::filesystem::exists(*file, existence_error);
    if (existence_error) {
        return failure(
            "Failed to inspect scene destination: " + existence_error.message()
        );
    }
    if (exists && !overwrite) {
        return failure("Scene asset already exists: " + path.as_string());
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
    auto metadata = database.ensure_native_asset(path);
    if (!metadata) {
        return failure(std::move(metadata.error()));
    }
    const bool changed_path = !session.path || *session.path != path;
    session.path = path;
    session.document = std::move(*document);
    session.dirty = false;
    session.external_change_pending = false;
    session.error = nullopt;
    if (auto status = watcher.acknowledge(); !status) {
        return status;
    }
    activity.record(
        OperationSource::User,
        changed_path ? "SaveSceneAs" : "SaveScene",
        path.as_string()
    );
    return {};
}

Status<std::string> save_scene(
    World& world,
    AssetDatabase& database,
    ProjectAssetWatcher& watcher,
    const ComponentOperations& operations,
    ActivityLog& activity,
    SceneSession& session
) {
    if (!session.path) {
        return failure(std::string("The current scene has no asset path"));
    }
    return save_scene_to(
        world,
        *session.path,
        true,
        database,
        watcher,
        operations,
        activity,
        session
    );
}

Status<std::string> save_scene_as(
    World& world,
    const AssetPath& path,
    bool overwrite,
    AssetDatabase& database,
    ProjectAssetWatcher& watcher,
    const ComponentOperations& operations,
    ActivityLog& activity,
    SceneSession& session
) {
    return save_scene_to(
        world,
        path,
        overwrite,
        database,
        watcher,
        operations,
        activity,
        session
    );
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

Status<std::string> reload_scene(
    World& world,
    const AssetDatabase& database,
    const ComponentOperations& operations,
    Selection& selection,
    ActivityLog& activity,
    SceneSession& session,
    OperationSource source
) {
    if (!session.path) {
        return failure(std::string("The current scene has no asset path"));
    }
    auto document = read_scene_document(*session.path, database);
    if (!document) {
        return failure(std::move(document.error()));
    }
    return replace_scene(
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

} // namespace fei::editor
