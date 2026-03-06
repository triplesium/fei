#include "pbr/light.hpp"

#include "math/matrix.hpp"
#include "pbr/mesh_view.hpp"
#include "rendering/view.hpp"

namespace fei {

void init_light_view_uniform_buffer(
    Query<Entity, DirectionalLight, Transform3d>::Filter<
        Without<ViewUniformBuffer>> query_light,
    Res<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, light, transform] : query_light) {
        auto buffer = device->create_buffer(BufferDescription {
            .size = sizeof(ViewUniform),
            .usages = BufferUsages::Uniform,
        });
        commands.entity(entity).add(ViewUniformBuffer {.buffer = buffer});
    }
}

void prepare_light_view_uniform_buffer(
    Query<Entity, DirectionalLight, Transform3d, ViewUniformBuffer> query_light,
    Res<GraphicsDevice> device
) {
    constexpr float proj_size = 25.0f;
    for (auto [entity, light, transform, view_uniform_buffer] : query_light) {
        auto view = look_at(
            transform.position,
            transform.position + transform.forward(),
            Vector3 {0.0f, 1.0f, 0.0f}
        );
        auto proj = orthographic(
            -proj_size,
            proj_size,
            proj_size,
            -proj_size,
            0.1f,
            2 * proj_size
        );
        view_uniform_buffer.uniform = ViewUniform {
            .clip_from_world = proj * view,
            .view_from_world = view,
            .clip_from_view = proj,
            .world_position = transform.position,
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
