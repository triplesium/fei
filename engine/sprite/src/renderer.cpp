#include "sprite/renderer.hpp"

#include "math/matrix.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace ets {

void SpritePhase::clear() {
    vertices.clear();
    indices.clear();
    batches.clear();
    clip_from_world = Matrix4x4::Identity;
    clear_color = {};
    active = false;
}

Matrix4x4 camera_2d_clip_from_world(
    const Camera2d& camera,
    const Transform2d& transform,
    uint32 target_width,
    uint32 target_height
) {
    return camera_2d_clip_from_world(
        camera,
        transform.model_matrix(),
        target_width,
        target_height
    );
}

Matrix4x4 camera_2d_clip_from_world(
    const Camera2d& camera,
    const Matrix4x4& world_from_camera,
    uint32 target_width,
    uint32 target_height
) {
    if (target_width == 0 || target_height == 0 ||
        camera.vertical_size <= 0.0f) {
        return Matrix4x4::Identity;
    }

    const float aspect_ratio =
        static_cast<float>(target_width) / static_cast<float>(target_height);
    const auto projection = orthographic(
        camera.vertical_size * aspect_ratio,
        camera.vertical_size,
        -1.0f,
        1.0f
    );
    return projection * world_from_camera.inverse_affine();
}

SpriteQuad
make_sprite_quad(const Sprite& sprite, const Transform2d& transform) {
    return make_sprite_quad(sprite, transform.model_matrix());
}

SpriteQuad
make_sprite_quad(const Sprite& sprite, const Matrix4x4& world_from_local) {
    const auto world_from_sprite =
        world_from_local * scale(sprite.size.x, sprite.size.y, 1.0f);
    const auto position = [&](float x, float y) {
        const auto transformed = world_from_sprite * Vector4 {x, y, 0.0f, 1.0f};
        return Vector2 {transformed.x, transformed.y};
    };
    const auto uv_left =
        sprite.flip_x ? sprite.uv_rect.max.x : sprite.uv_rect.min.x;
    const auto uv_right =
        sprite.flip_x ? sprite.uv_rect.min.x : sprite.uv_rect.max.x;
    const auto uv_bottom =
        sprite.flip_y ? sprite.uv_rect.max.y : sprite.uv_rect.min.y;
    const auto uv_top =
        sprite.flip_y ? sprite.uv_rect.min.y : sprite.uv_rect.max.y;

    return SpriteQuad {
        .vertices =
            {
                SpriteVertex {
                    .position = position(-0.5f, -0.5f),
                    .uv = {uv_left, uv_bottom},
                    .color = sprite.color,
                },
                SpriteVertex {
                    .position = position(0.5f, -0.5f),
                    .uv = {uv_right, uv_bottom},
                    .color = sprite.color,
                },
                SpriteVertex {
                    .position = position(-0.5f, 0.5f),
                    .uv = {uv_left, uv_top},
                    .color = sprite.color,
                },
                SpriteVertex {
                    .position = position(0.5f, 0.5f),
                    .uv = {uv_right, uv_top},
                    .color = sprite.color,
                },
            },
        .indices = {0, 1, 2, 2, 1, 3},
    };
}

bool is_sprite_quad_visible(
    const SpriteQuad& quad,
    const Matrix4x4& clip_from_world
) {
    std::array<Vector4, 4> clip_positions {};
    for (std::size_t index = 0; index < quad.vertices.size(); ++index) {
        const auto position = quad.vertices[index].position;
        clip_positions[index] =
            clip_from_world * Vector4 {position.x, position.y, 0.0f, 1.0f};
    }

    const auto all_outside = [&](const auto& predicate) {
        return std::ranges::all_of(clip_positions, predicate);
    };
    return !(
        all_outside([](const Vector4& position) {
            return position.x < -position.w;
        }) ||
        all_outside([](const Vector4& position) {
            return position.x > position.w;
        }) ||
        all_outside([](const Vector4& position) {
            return position.y < -position.w;
        }) ||
        all_outside([](const Vector4& position) {
            return position.y > position.w;
        })
    );
}

void append_sprite_quad(SpritePhase& phase, const SpriteQuad& quad) {
    const auto base_vertex = static_cast<std::uint32_t>(phase.vertices.size());
    for (const auto& vertex : quad.vertices) {
        phase.vertices.push_back(vertex);
    }
    for (const auto index : quad.indices) {
        phase.indices.push_back(base_vertex + index);
    }
}

void append_sprite_quad(
    SpritePhase& phase,
    const Sprite& sprite,
    const Transform2d& transform
) {
    append_sprite_quad(phase, make_sprite_quad(sprite, transform));
}

void append_sprite(
    SpritePhase& phase,
    const SpriteQuad& quad,
    std::shared_ptr<const ResourceSet> texture_set
) {
    const auto first_index = static_cast<uint32>(phase.indices.size());
    append_sprite_quad(phase, quad);

    constexpr uint32 quad_index_count = 6;
    if (!phase.batches.empty() &&
        phase.batches.back().texture_set == texture_set) {
        phase.batches.back().index_count += quad_index_count;
        return;
    }
    phase.batches.push_back(
        SpriteBatch {
            .texture_set = std::move(texture_set),
            .first_index = first_index,
            .index_count = quad_index_count,
        }
    );
}

void append_sprite(
    SpritePhase& phase,
    const Sprite& sprite,
    const Transform2d& transform,
    std::shared_ptr<const ResourceSet> texture_set
) {
    append_sprite(
        phase,
        make_sprite_quad(sprite, transform),
        std::move(texture_set)
    );
}

} // namespace ets
