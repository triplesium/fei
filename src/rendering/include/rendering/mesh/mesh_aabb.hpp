#pragma once
#include "asset/assets.hpp"
#include "ecs/commands.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "math/primitives.hpp"
#include "rendering/components.hpp"

namespace fei {

void compute_mesh_aabb(
    Query<Entity, const Mesh3d>::Filter<Without<Aabb>> query,
    ResRO<Assets<Mesh>> meshes,
    Commands commands
);

}
