#include "pbr/light.hpp"

#include "math/matrix.hpp"
#include "pbr/mesh_view.hpp"
#include "rendering/view.hpp"

namespace fei {

void init_light_view_uniform_buffer(
    Query<Entity, const DirectionalLight, const Transform3d>::Filter<
        Without<ViewUniformBuffer>> query_light,
    ResRO<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, light, transform] : query_light) {
        auto buffer = device->create_buffer(
            BufferDescription {
                .size = sizeof(ViewUniform),
                .usages = BufferUsages::Uniform,
            }
        );
        commands.entity(entity).add(ViewUniformBuffer {.buffer = buffer});
    }
}

void prepare_light_view_uniform_buffer(
    Query<Entity, const DirectionalLight, const Transform3d, ViewUniformBuffer>
        query_light,
    ResRO<GraphicsDevice> device
) {
    for (auto [entity, light, transform, view_uniform_buffer] : query_light) {
        auto view = look_at(
            transform.position,
            transform.position + transform.forward(),
            Vector3 {0.0f, 1.0f, 0.0f}
        );
        const float proj_size = light.projection_size;
        auto proj = orthographic(
            -proj_size,
            proj_size,
            proj_size,
            -proj_size,
            0.1f,
            2 * proj_size
        );
        auto uniform = ViewUniform {
            .clip_from_world = proj * view,
            .view_from_world = view,
            .clip_from_view = proj,
            .world_position = transform.position,
        };
        view_uniform_buffer.uniform = uniform;
        view_uniform_buffer.view = RenderView {
            .kind = RenderViewKind::DirectionalShadow,
            .id = ViewId::from_source(entity),
            .clip_from_world = uniform.clip_from_world,
            .view_from_world = uniform.view_from_world,
            .clip_from_view = uniform.clip_from_view,
            .world_position = uniform.world_position,
            .frustum = extract_frustum(uniform.clip_from_world),
        };
        device->update_buffer(
            view_uniform_buffer.buffer,
            0,
            &view_uniform_buffer.uniform,
            sizeof(ViewUniform)
        );
    }
}
} // namespace fei
