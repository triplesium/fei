#pragma once

#include "base/types.hpp"
#include "core/transform.hpp"
#include "math/color.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "rendering/gpu_vector.hpp"
#include "sprite/components.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ets {

class Buffer;
class ResourceLayout;
class ResourceSet;
enum class CachedRenderPipelineId : uint32;
enum class PixelFormat : uint8;

struct SpriteVertex {
    Vector2 position;
    Vector2 uv;
    Color4F color;
};

struct SpriteQuad {
    std::array<SpriteVertex, 4> vertices;
    std::array<std::uint32_t, 6> indices;
};

struct SpriteBatch {
    std::shared_ptr<const ResourceSet> texture_set;
    uint32 first_index {0};
    uint32 index_count {0};
};

struct SpritePhase {
    GpuVector<SpriteVertex> vertices {BufferUsages::Vertex};
    GpuVector<std::uint32_t> indices {BufferUsages::Index};
    std::vector<SpriteBatch> batches;
    Matrix4x4 clip_from_world;
    Color4F clear_color;
    bool active {false};

    void clear();
};

[[nodiscard]] Matrix4x4 camera_2d_clip_from_world(
    const Camera2d& camera,
    const Transform2d& transform,
    uint32 target_width,
    uint32 target_height
);

[[nodiscard]] Matrix4x4 camera_2d_clip_from_world(
    const Camera2d& camera,
    const Matrix4x4& world_from_camera,
    uint32 target_width,
    uint32 target_height
);

[[nodiscard]] SpriteQuad
make_sprite_quad(const Sprite& sprite, const Transform2d& transform);

[[nodiscard]] SpriteQuad
make_sprite_quad(const Sprite& sprite, const Matrix4x4& world_from_local);

[[nodiscard]] bool is_sprite_quad_visible(
    const SpriteQuad& quad,
    const Matrix4x4& clip_from_world
);

void append_sprite_quad(SpritePhase& phase, const SpriteQuad& quad);

void append_sprite_quad(
    SpritePhase& phase,
    const Sprite& sprite,
    const Transform2d& transform
);

void append_sprite(
    SpritePhase& phase,
    const SpriteQuad& quad,
    std::shared_ptr<const ResourceSet> texture_set
);

void append_sprite(
    SpritePhase& phase,
    const Sprite& sprite,
    const Transform2d& transform,
    std::shared_ptr<const ResourceSet> texture_set
);

struct SpriteRenderState {
    std::shared_ptr<ResourceLayout> view_layout;
    std::shared_ptr<ResourceLayout> texture_layout;
    std::shared_ptr<Buffer> view_uniform_buffer;
    std::shared_ptr<ResourceSet> view_resource_set;
    std::optional<PixelFormat> pipeline_format;
    std::optional<CachedRenderPipelineId> pipeline_id;
};

} // namespace ets
