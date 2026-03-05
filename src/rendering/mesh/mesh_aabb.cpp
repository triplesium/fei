#include "rendering/mesh/mesh_aabb.hpp"

#include "core/aabb.hpp"
#include "rendering/mesh/mesh.hpp"

namespace fei {

void compute_mesh_aabb(
    Query<Entity, Mesh3d>::Filter<Without<Aabb>> query,
    Res<Assets<Mesh>> meshes,
    Commands commands
) {
    for (const auto& [entity, mesh3d] : query) {
        auto& mesh = meshes->get(mesh3d.mesh).value();
        const auto& positions =
            mesh.get_attribute(Mesh::ATTRIBUTE_POSITION.id).as_float3().value();

        Vector3 min {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        Vector3 max {
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
        };

        for (const auto& pos : positions) {
            min.x = std::min(min.x, pos[0]);
            min.y = std::min(min.y, pos[1]);
            min.z = std::min(min.z, pos[2]);
            max.x = std::max(max.x, pos[0]);
            max.y = std::max(max.y, pos[1]);
            max.z = std::max(max.z, pos[2]);
        }

        commands.entity(entity).add(Aabb {
            .min = {min.x, min.y, min.z},
            .max = {max.x, max.y, max.z},
        });
    }
}

} // namespace fei
