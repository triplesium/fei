#include "rendering/view.hpp"

#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "math/matrix.hpp"

namespace fei {

void init_camera_view_uniform(
    Query<Entity, const Camera3d, const Transform3d>::Filter<
        Without<ViewUniformBuffer>> query,
    ResRO<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, camera, transform] : query) {
        auto buffer = device->create_buffer(
            BufferDescription {
                .size = sizeof(ViewUniform),
                .usages = BufferUsages::Uniform,
            }
        );
        commands.entity(entity).add(ViewUniformBuffer {.buffer = buffer});
    }
}

void prepare_camera_view_uniform(
    Query<Entity, const Camera3d, const Transform3d, ViewUniformBuffer> query,
    ResRO<GraphicsDevice> device
) {
    for (auto [entity, camera, transform, view_uniform_buffer_component] :
         query) {
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
        auto uniform = ViewUniform {
            .clip_from_world = projection * view,
            .view_from_world = view,
            .clip_from_view = projection,
            .world_position = transform.position,
        };
        view_uniform_buffer_component.uniform = uniform;
        view_uniform_buffer_component.view = RenderView {
            .kind = RenderViewKind::Camera,
            .id = ViewId::from_source(entity),
            .clip_from_world = uniform.clip_from_world,
            .view_from_world = uniform.view_from_world,
            .clip_from_view = uniform.clip_from_view,
            .world_position = uniform.world_position,
            .frustum = extract_frustum(uniform.clip_from_world),
        };
        device->update_buffer(
            view_uniform_buffer_component.buffer,
            0,
            &view_uniform_buffer_component.uniform,
            sizeof(ViewUniform)
        );
    }
}

} // namespace fei
