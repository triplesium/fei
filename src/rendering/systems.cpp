#include "rendering/systems.hpp"

namespace fei {

void render_mesh(
    Query<Entity, Mesh3d, MeshMaterial3d, Transform3d> query,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<GraphicsDevice> device,
    Res<ViewResource> view_resource,
    Res<MeshUniforms> mesh_uniforms
) {
    for (const auto& [entity, mesh3d, material3d, transform3d] : query) {
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh.id());
        auto material_opt = materials->get(material3d.material.id());
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        // TODO: Pipeline caching
        auto pipeline = device->create_render_pipeline(PipelineDescription {
            .blend_state = BlendStateDescription::SingleAlphaBlend,
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyGreaterEqual,
            .rasterizer_state = {},
            .render_primitive = gpu_mesh.primitive(),
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {gpu_mesh.vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders = material.shaders(),
                },
        });
        auto command_buffer = device->create_command_buffer();
        command_buffer->begin();
        command_buffer->set_pipeline(pipeline);
        command_buffer->set_resource_set(0, material.resource_set());
        command_buffer->set_resource_set(1, view_resource->resource_set);
        command_buffer->set_resource_set(
            2,
            mesh_uniforms->entries.at(entity).resource_set
        );
        command_buffer->set_vertex_buffer(gpu_mesh.vertex_buffer());
        if (auto index_buffer = gpu_mesh.index_buffer()) {
            command_buffer->set_index_buffer(
                *index_buffer,
                IndexFormat::Uint32
            );
            command_buffer->draw_indexed(
                gpu_mesh.index_buffer_size() / sizeof(std::uint32_t)
            );
        } else {
            command_buffer->draw(0, gpu_mesh.vertex_count());
        }
        command_buffer->end();
        device->submit_commands(command_buffer);
    }
}

} // namespace fei
