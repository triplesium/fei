#include "core/transform.hpp"

#include "app/app.hpp"
#include "base/debug.hpp"
#include "core/transform_plugin.hpp"
#include "ecs/system_config.hpp"

#include <tuple>
#include <unordered_set>
#include <vector>

namespace fei {

namespace {

void resolve_global_transform(
    Query<Entity, const Transform3d, GlobalTransform3d>& transforms,
    Query<Entity, const ChildOf>& parents,
    Query<Entity, const GlobalTransform3d>::Filter<Without<Transform3d>>&
        explicit_globals,
    Entity entity,
    std::unordered_set<Entity>& resolved
) {
    if (resolved.contains(entity)) {
        return;
    }

    std::vector<Entity> chain;
    Matrix4x4 parent_matrix {Matrix4x4::Identity};
    auto current = entity;

    while (true) {
        if (resolved.contains(current)) {
            auto item = transforms.get(current);
            FEI_ASSERT(item);
            parent_matrix = std::get<2>(*item).read().to_matrix();
            break;
        }

        auto transform_item = transforms.get(current);
        if (!transform_item) {
            if (auto global_item = explicit_globals.get(current)) {
                parent_matrix = std::get<1>(*global_item).to_matrix();
                break;
            }
            auto parent_item = parents.get(current);
            if (!parent_item) {
                break;
            }
            current = std::get<1>(*parent_item).parent;
            continue;
        }

        chain.push_back(current);

        auto parent_item = parents.get(current);
        if (!parent_item) {
            break;
        }
        current = std::get<1>(*parent_item).parent;
    }

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto chain_entity = *it;
        auto item = transforms.get(chain_entity);
        FEI_ASSERT(item);
        parent_matrix = parent_matrix * std::get<1>(*item).to_matrix();
        auto& global_transform = std::get<2>(*item);
        if (global_transform.read().matrix != parent_matrix) {
            global_transform.write().matrix = parent_matrix;
        }
        resolved.insert(chain_entity);
    }
}

} // namespace

void sync_global_transforms(
    Query<Entity, const Transform3d>::Filter<Without<GlobalTransform3d>> query,
    Commands commands
) {
    for (const auto& [entity, transform] : query) {
        (void)transform;
        commands.entity(entity).add(GlobalTransform3d {});
    }
}

void propagate_transforms(
    Query<Entity, const Transform3d, GlobalTransform3d> transforms,
    Query<Entity, const ChildOf> parents,
    Query<Entity, const GlobalTransform3d>::Filter<Without<Transform3d>>
        explicit_globals
) {
    std::unordered_set<Entity> resolved;
    resolved.reserve(transforms.size());
    for (const auto& [entity, transform, global_transform] : transforms) {
        (void)transform;
        (void)global_transform;
        resolve_global_transform(
            transforms,
            parents,
            explicit_globals,
            entity,
            resolved
        );
    }
}

void TransformPlugin::setup(App& app) {
    app.add_systems(
        PostUpdate,
        chain(sync_global_transforms, propagate_transforms) |
            in_set<TransformSystems::Propagate>()
    );
}

} // namespace fei
