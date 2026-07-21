#include "rendering/view.hpp"

#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "math/matrix.hpp"

namespace fei {

void init_camera_view_uniform(
    Query<Entity, const Camera3d, const GlobalTransform3d>::Filter<
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
    Query<Entity, const Camera3d, const GlobalTransform3d, ViewUniformBuffer>
        query,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue
) {
    for (auto [entity, camera, transform, view_uniform_buffer_component] :
         query) {
        auto world_position = transform.translation();
        auto view = look_at(
            world_position,
            world_position + transform.forward(),
            transform.up()
        );
        auto projection = perspective(
            camera.fov_y * DEG2RAD,
            camera.aspect_ratio,
            camera.near_plane,
            camera.far_plane
        );
        auto logical_clip_from_world = projection * view;
        auto clip_space_transform = device->clip_space_transform();
        auto uniform = ViewUniform {
            .clip_from_world = clip_space_transform * logical_clip_from_world,
            .view_from_world = view,
            .clip_from_view = clip_space_transform * projection,
            .world_from_view = view.inverse_affine(),
            .view_from_clip = (clip_space_transform * projection).inverse(),
            .world_position = world_position,
        };
        auto& view_uniform_buffer = view_uniform_buffer_component.write();
        view_uniform_buffer.uniform = uniform;
        view_uniform_buffer.view = RenderView {
            .kind = RenderViewKind::Camera,
            .id = ViewId::from_source(entity),
            .clip_from_world = logical_clip_from_world,
            .view_from_world = uniform.view_from_world,
            .clip_from_view = projection,
            .world_position = uniform.world_position,
            .frustum = extract_frustum(logical_clip_from_world),
        };
        render_queue->write_buffer(
            view_uniform_buffer.buffer,
            0,
            &view_uniform_buffer.uniform,
            sizeof(ViewUniform)
        );
    }
}

} // namespace fei
