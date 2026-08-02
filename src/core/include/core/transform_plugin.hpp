#pragma once

#include "app/plugin.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/query.hpp"
#include "ecs/system_set.hpp"

namespace fei {

class App;

struct TransformSystems {
    struct Propagate : SystemSet<Propagate> {};
};

void sync_global_transforms_2d(
    Query<Entity, const Transform2d>::Filter<Without<GlobalTransform2d>> query,
    Commands commands
);

void propagate_transforms_2d(
    Query<Entity, const Transform2d, GlobalTransform2d> transforms,
    Query<Entity, const ChildOf> parents,
    Query<Entity, const GlobalTransform2d>::Filter<Without<Transform2d>>
        explicit_globals
);

void sync_global_transforms(
    Query<Entity, const Transform3d>::Filter<Without<GlobalTransform3d>> query,
    Commands commands
);

void propagate_transforms(
    Query<Entity, const Transform3d, GlobalTransform3d> transforms,
    Query<Entity, const ChildOf> parents,
    Query<Entity, const GlobalTransform3d>::Filter<Without<Transform3d>>
        explicit_globals
);

class TransformPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
