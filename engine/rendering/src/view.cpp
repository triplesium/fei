#include "rendering/view.hpp"

#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "math/matrix.hpp"

#include <vector>

namespace ets {

void init_camera_view_uniform(
    Query<Entity, const Camera3d, const GlobalTransform3d>::Filter<
        Without<PreparedView>> query,
    Commands commands
) {
    for (auto [entity, camera, transform] : query) {
        (void)camera;
        (void)transform;
        commands.entity(entity).add(PreparedView {});
    }
}

void prepare_camera_view_uniform(
    Query<Entity, const Camera3d, const GlobalTransform3d, PreparedView> query,
    ResRO<GraphicsDevice> device
) {
    for (auto [entity, camera, transform, prepared_view_component] : query) {
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
        auto& prepared_view = prepared_view_component.write();
        prepared_view.uniform = uniform;
        prepared_view.view = RenderView {
            .kind = RenderViewKind::Camera,
            .id = ViewId::from_source(entity),
            .clip_from_world = logical_clip_from_world,
            .view_from_world = uniform.view_from_world,
            .clip_from_view = projection,
            .world_position = uniform.world_position,
            .frustum = extract_frustum(logical_clip_from_world),
        };
    }
}

void upload_view_uniforms(
    Query<PreparedView> query,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRW<ViewUniforms> uniforms
) {
    uniforms->buffer.initialize(*device);
    uniforms->buffer.clear();

    std::vector<PreparedView*> prepared_views;
    std::vector<ViewUniform> values;
    for (auto [prepared_view_component] : query) {
        auto& prepared_view = prepared_view_component.write();
        prepared_views.push_back(&prepared_view);
        values.push_back(prepared_view.uniform);
    }

    const auto first_offset = uniforms->buffer.append(values);
    for (std::size_t index = 0; index < prepared_views.size(); ++index) {
        prepared_views[index]->dynamic_offset = static_cast<uint32>(
            first_offset + index * uniforms->buffer.stride()
        );
    }
    uniforms->buffer.upload(*device, *render_queue);
}

} // namespace ets
