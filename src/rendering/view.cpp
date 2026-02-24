#include "rendering/view.hpp"

#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "math/matrix.hpp"

namespace fei {

void init_camera_view_uniform(
    Query<Entity, Camera3d, Transform3d>::Filter<Without<ViewUniformBuffer>>
        query,
    Res<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, camera, transform] : query) {
        auto buffer = device->create_buffer(BufferDescription {
            .size = sizeof(ViewUniform),
            .usages = BufferUsages::Uniform,
        });
        commands.entity(entity).add(ViewUniformBuffer {buffer});
    }
}

void prepare_camera_view_uniform(
    Query<Camera3d, Transform3d, ViewUniformBuffer> query,
    Res<GraphicsDevice> device
) {
    for (auto [camera, transform, view_uniform_buffer_component] : query) {
        auto view = look_at(
            transform.position,
            transform.position + transform.forward(),
            Vector3 {0.0f, 1.0f, 0.0f}
        );
        auto projection = perspective(
            camera.fov_y * DEG2RAD,
            camera.aspect_ratio,
            camera.near_plane,
            camera.far_plane
        );
        ViewUniform uniform {
            .clip_from_world = projection * view,
            .view_from_world = view,
            .clip_from_view = projection,
            .world_position = transform.position,
        };
        device->update_buffer(
            view_uniform_buffer_component.buffer,
            0,
            &uniform,
            sizeof(ViewUniform)
        );
    }
}

} // namespace fei
