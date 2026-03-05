#pragma once
#include "asset/assets.hpp"
#include "core/aabb.hpp"
#include "ecs/commands.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "rendering/components.hpp"

namespace fei {

void compute_mesh_aabb(
    Query<Entity, Mesh3d>::Filter<Without<Aabb>> query,
    Res<Assets<Mesh>> meshes,
    Commands commands
);

}
