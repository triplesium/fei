#pragma once
#include "core/camera.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <memory>

namespace fei {

struct alignas(16) ViewUniform {
    Matrix4x4 clip_from_world;
    Matrix4x4 view_from_world;
    Matrix4x4 clip_from_view;
    Vector3 world_position;
};

struct ViewResource {
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceSet> resource_set;
    std::shared_ptr<ResourceLayout> resource_layout;
};

void init_view_resource(
    Res<GraphicsDevice> device,
    Res<ViewResource> view_resource
);

void prepare_view_resource(
    Res<GraphicsDevice> device,
    Res<ViewResource> view_uniform,
    Query<Camera3d, Transform3d> query
);

} // namespace fei
