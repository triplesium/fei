#include "rendering/view.hpp"

#include "math/matrix.hpp"

namespace fei {

void init_view_resource(
    Res<GraphicsDevice> device,
    Res<ViewResource> view_resource
) {
    view_resource->uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(ViewUniform),
        .usages = BufferUsages::Uniform,
    });

    view_resource->resource_layout =
        device->create_resource_layout(ResourceLayoutDescription {
            .elements =
                {
                    ResourceLayoutElementDescription {
                        // NOTE: Temporary binding point
                        .binding = 1,
                        .kind = ResourceKind::UniformBuffer,
                        .stages =
                            {ShaderStages::Vertex, ShaderStages::Fragment},
                    },
                },
        });

    view_resource->resource_set =
        device->create_resource_set(ResourceSetDescription {
            .layout = view_resource->resource_layout,
            .resources = {view_resource->uniform_buffer},
        });
}

void prepare_view_resource(
    Res<GraphicsDevice> device,
    Res<ViewResource> view_uniform,
    Query<Camera3d, Transform3d> query
) {
    if (query.empty()) {
        return;
    }
    auto [camera, transform] = query.first();
    auto view = transform.to_matrix().inverse_affine();
    auto projection = perspective(
        camera.fov_y * DEG2RAD,
        camera.aspect_ratio,
        camera.near_plane,
        camera.far_plane
    );
    ViewUniform uniform {
        .view_projection = projection * view,
    };
    device->update_buffer(
        view_uniform->uniform_buffer,
        0,
        &uniform,
        sizeof(ViewUniform)
    );
}

} // namespace fei
