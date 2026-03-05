#include "rendering/mesh/mesh_uniform.hpp"

#include "graphics/enums.hpp"
#include "graphics/resource.hpp"
#include "rendering/components.hpp"

namespace fei {

void prepare_mesh_uniforms(
    Query<Entity, Mesh3d, Transform3d> query,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms
) {
    // TODO: Cleanup unused uniforms
    for (const auto& [entity, mesh3d, transform3d] : query) {
        MeshUniform uniform {
            .world_from_local = transform3d.to_matrix(),
        };

        if (!mesh_uniforms->entries.contains(entity)) {
            MeshUniforms::Entry entry;
            entry.entity = entity;
            entry.uniform_buffer = device->create_buffer(BufferDescription {
                .size = sizeof(MeshUniform),
                .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
            });
            entry.resource_layout = device->create_resource_layout(
                ResourceLayoutDescription::sequencial(
                    {ShaderStages::Vertex, ShaderStages::Fragment},
                    {uniform_buffer("Mesh")}
                )
            );
            entry.resource_set =
                device->create_resource_set(ResourceSetDescription {
                    .layout = entry.resource_layout,
                    .resources = {entry.uniform_buffer},
                });
            mesh_uniforms->entries[entity] = std::move(entry);
        }
        device->update_buffer(
            mesh_uniforms->entries[entity].uniform_buffer,
            0,
            &uniform,
            sizeof(MeshUniform)
        );
    }
}

} // namespace fei
