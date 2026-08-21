#include "ui_rendering/renderer.hpp"

namespace fei::ui::rendering {

namespace {

Optional<Quad> make_colored_quad(
    Rect bounds,
    Rect uv,
    Color4F color,
    const ComputedNode& style,
    float kind,
    float border_side,
    Optional<Rect> clip
) {
    const auto original = bounds;
    if (clip) {
        bounds.min.x = max(bounds.min.x, clip->min.x);
        bounds.min.y = max(bounds.min.y, clip->min.y);
        bounds.max.x = min(bounds.max.x, clip->max.x);
        bounds.max.y = min(bounds.max.y, clip->max.y);
    }
    if (bounds.min.x >= bounds.max.x || bounds.min.y >= bounds.max.y) {
        return nullopt;
    }

    const auto original_size = original.max - original.min;
    const auto original_center = (original.min + original.max) * 0.5f;
    const auto uv_size = uv.max - uv.min;
    const auto uv_at = [&](Vector2 position) {
        return Vector2 {
            uv.min.x +
                (position.x - original.min.x) / original_size.x * uv_size.x,
            uv.min.y +
                (position.y - original.min.y) / original_size.y * uv_size.y,
        };
    };
    const Vector2 top_left {bounds.min.x, bounds.min.y};
    const Vector2 top_right {bounds.max.x, bounds.min.y};
    const Vector2 bottom_left {bounds.min.x, bounds.max.y};
    const Vector2 bottom_right {bounds.max.x, bounds.max.y};
    const Vector4 radius {
        style.border_radius.top_left,
        style.border_radius.top_right,
        style.border_radius.bottom_right,
        style.border_radius.bottom_left,
    };
    const Vector4 border {
        style.border.left,
        style.border.top,
        style.border.right,
        style.border.bottom,
    };
    const auto vertex = [&](Vector2 vertex_position) {
        return Vertex {
            .position = vertex_position,
            .uv = uv_at(vertex_position),
            .color = color,
            .local_position = vertex_position - original_center,
            .size = original_size,
            .border_radius = radius,
            .border = border,
            .kind = kind,
            .border_side = border_side,
        };
    };
    return Quad {
        .vertices =
            {
                vertex(top_left),
                vertex(top_right),
                vertex(bottom_left),
                vertex(bottom_right),
            },
        .indices = {0, 1, 2, 2, 1, 3},
    };
}

} // namespace

Optional<Quad> make_quad(
    const ComputedNode& node,
    const BackgroundColor& background,
    Optional<Rect> clip
) {
    if (node.size.x <= 0.0f || node.size.y <= 0.0f) {
        return nullopt;
    }
    return make_colored_quad(
        Rect {.min = node.position, .max = node.position + node.size},
        Rect {.min = Vector2::Zero, .max = Vector2::One},
        background.color,
        node,
        0.0f,
        0.0f,
        clip
    );
}

Optional<Quad> make_image_quad(
    const ComputedNode& node,
    const ImageNode& image,
    Vector2 texture_size,
    Optional<Rect> clip
) {
    if (node.size.x <= 0.0f || node.size.y <= 0.0f || texture_size.x <= 0.0f ||
        texture_size.y <= 0.0f) {
        return nullopt;
    }
    const auto source = image.source_rect.value_or(
        Rect {.min = Vector2::Zero, .max = texture_size}
    );
    const auto source_size = source.max - source.min;
    if (source_size.x <= 0.0f || source_size.y <= 0.0f) {
        return nullopt;
    }

    Vector2 draw_size = node.size;
    if (image.mode == NodeImageMode::Auto) {
        const auto scale =
            min(node.size.x / source_size.x, node.size.y / source_size.y);
        draw_size = source_size * scale;
    }
    const auto draw_position = node.position + (node.size - draw_size) * 0.5f;
    Rect uv {
        .min =
            {
                source.min.x / texture_size.x,
                1.0f - source.min.y / texture_size.y,
            },
        .max = {
            source.max.x / texture_size.x,
            1.0f - source.max.y / texture_size.y,
        },
    };
    if (image.flip_x) {
        std::swap(uv.min.x, uv.max.x);
    }
    if (image.flip_y) {
        std::swap(uv.min.y, uv.max.y);
    }
    return make_colored_quad(
        Rect {.min = draw_position, .max = draw_position + draw_size},
        uv,
        image.color,
        node,
        0.0f,
        0.0f,
        clip
    );
}

Optional<Quad> make_border_quad(
    const ComputedNode& node,
    Color4F color,
    BorderSide side,
    Optional<Rect> clip
) {
    const auto width = side == BorderSide::Left  ? node.border.left :
                       side == BorderSide::Top   ? node.border.top :
                       side == BorderSide::Right ? node.border.right :
                                                   node.border.bottom;
    if (width <= 0.0f || color.a <= 0.0f || node.size.x <= 0.0f ||
        node.size.y <= 0.0f) {
        return nullopt;
    }
    return make_colored_quad(
        Rect {.min = node.position, .max = node.position + node.size},
        Rect {.min = Vector2::Zero, .max = Vector2::One},
        color,
        node,
        1.0f,
        static_cast<float>(side),
        clip
    );
}

Optional<Quad> make_glyph_quad(
    const ComputedNode& node,
    const text::PositionedGlyph& glyph,
    Color4F color,
    Optional<Rect> clip
) {
    if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f || color.a <= 0.0f) {
        return nullopt;
    }
    const auto position = node.content_position + glyph.position;
    return make_colored_quad(
        Rect {.min = position, .max = position + glyph.size},
        glyph.uv,
        color,
        node,
        2.0f,
        0.0f,
        clip
    );
}

void Phase::clear() {
    vertices.clear();
    indices.clear();
    batches.clear();
    glyph_count = 0;
    glyph_batch_count = 0;
    active = false;
}

void Phase::append(
    const Quad& quad,
    std::shared_ptr<const ResourceSet> texture_set
) {
    const auto first_index = static_cast<uint32>(indices.size());
    const auto base_vertex = static_cast<std::uint32_t>(vertices.size());
    for (const auto& vertex : quad.vertices) {
        vertices.push_back(vertex);
    }
    for (const auto index : quad.indices) {
        indices.push_back(base_vertex + index);
    }
    constexpr uint32 index_count = 6;
    if (!batches.empty() && batches.back().texture_set == texture_set) {
        batches.back().index_count += index_count;
    } else {
        batches.push_back(
            Batch {
                .texture_set = std::move(texture_set),
                .first_index = first_index,
                .index_count = index_count,
            }
        );
    }
}

void Phase::append_glyph(
    const Quad& quad,
    std::shared_ptr<const ResourceSet> texture_set
) {
    const auto previous_batch_count = batches.size();
    append(quad, std::move(texture_set));
    ++glyph_count;
    if (batches.size() > previous_batch_count) {
        ++glyph_batch_count;
    }
}

} // namespace fei::ui::rendering
