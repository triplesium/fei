#pragma once
#include "core/camera.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "rendering/dynamic_uniform_buffer.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/visibility.hpp"

namespace fei {

struct alignas(16) ViewUniform {
    Matrix4x4 clip_from_world;
    Matrix4x4 view_from_world;
    Matrix4x4 clip_from_view;
    Matrix4x4 world_from_view;
    Matrix4x4 view_from_clip;
    Vector3 world_position;
};

struct PreparedView {
    ViewUniform uniform {};
    RenderView view;
    uint32 dynamic_offset {};
};

struct ViewUniforms {
    DynamicUniformBuffer<ViewUniform> buffer;
};

void init_camera_view_uniform(
    Query<Entity, const Camera3d, const GlobalTransform3d>::Filter<
        Without<PreparedView>> query,
    Commands commands
);

void prepare_camera_view_uniform(
    Query<Entity, const Camera3d, const GlobalTransform3d, PreparedView> query,
    ResRO<GraphicsDevice> device
);

void upload_view_uniforms(
    Query<PreparedView> query,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRW<ViewUniforms> uniforms
);

} // namespace fei
