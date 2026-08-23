#include "rendering/mesh/mesh_uniform.hpp"

#include "graphics/enums.hpp"
#include "graphics/resource.hpp"
#include "rendering/components.hpp"

namespace ets {

void prepare_mesh_uniforms(
    Query<Entity, const Mesh3d, const GlobalTransform3d> query,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRW<MeshUniforms> mesh_uniforms
) {
    if (!mesh_uniforms->resource_layout) {
        auto mesh_binding = uniform_buffer("mesh");
        mesh_binding.options.set(ResourceLayoutElementOptions::DynamicBinding);
        mesh_uniforms->resource_layout = device->create_resource_layout(
            ResourceLayoutDescription::sequencial(
                {ShaderStages::Vertex, ShaderStages::Fragment},
                {std::move(mesh_binding)}
            )
        );
    }

    mesh_uniforms->uniforms.initialize(*device);
    mesh_uniforms->entries.clear();
    mesh_uniforms->uniforms.clear();

    for (const auto& [entity, mesh3d, global_transform] : query) {
        (void)mesh3d;
        MeshUniform uniform {
            .world_from_local = global_transform.to_matrix(),
        };

        mesh_uniforms->entries.emplace(
            entity,
            MeshUniforms::Entry {
                .dynamic_offset = mesh_uniforms->uniforms.push_back(uniform),
            }
        );
    }

    mesh_uniforms->uniforms.upload(*device, *render_queue);
    if (mesh_uniforms->uniforms.buffer() &&
        (!mesh_uniforms->resource_set ||
         mesh_uniforms->resource_set_buffer_revision !=
             mesh_uniforms->uniforms.buffer_revision())) {
        mesh_uniforms->resource_set = device->create_resource_set(
            ResourceSetDescription {
                .layout = mesh_uniforms->resource_layout,
                .resources = {mesh_uniforms->uniforms.binding()},
                .name = "mesh_uniforms",
            }
        );
        mesh_uniforms->resource_set_buffer_revision =
            mesh_uniforms->uniforms.buffer_revision();
    }
}

} // namespace ets
