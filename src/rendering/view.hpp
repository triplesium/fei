#pragma once
#include "core/camera.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
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

struct ViewUniformBuffer {
    ViewUniform uniform;
    std::shared_ptr<Buffer> buffer;
};

void init_camera_view_uniform(
    Query<Entity, Camera3d, Transform3d>::Filter<Without<ViewUniformBuffer>>
        query,
    Res<GraphicsDevice> device,
    Commands commands
);

void prepare_camera_view_uniform(
    Query<Camera3d, Transform3d, ViewUniformBuffer> query,
    Res<GraphicsDevice> device
);

} // namespace fei
