#include "ui/surface.hpp"

#include <algorithm>
#include <utility>

namespace ets::ui {

namespace {

struct ResolvedEdges {
    float left {0.0f};
    float right {0.0f};
    float top {0.0f};
    float bottom {0.0f};
};

float resolve(Length length, float reference, float auto_value = 0.0f) {
    switch (length.unit) {
        case LengthUnit::Auto:
            return auto_value;
        case LengthUnit::Px:
            return length.value;
        case LengthUnit::Percent:
            return reference * length.value * 0.01f;
    }
    return auto_value;
}

float clamp_dimension(
    float value,
    Length minimum,
    Length maximum,
    float reference
) {
    if (!minimum.is_auto()) {
        value = std::max(value, resolve(minimum, reference));
    }
    if (!maximum.is_auto()) {
        value = std::min(value, resolve(maximum, reference));
    }
    return std::max(value, 0.0f);
}

ResolvedEdges resolve_edges(const Edges& edges, Vector2 reference) {
    return {
        .left = resolve(edges.left, reference.x),
        .right = resolve(edges.right, reference.x),
        .top = resolve(edges.top, reference.y),
        .bottom = resolve(edges.bottom, reference.y),
    };
}

float main_axis(Vector2 value, FlexDirection direction) {
    return direction == FlexDirection::Row ? value.x : value.y;
}

float cross_axis(Vector2 value, FlexDirection direction) {
    return direction == FlexDirection::Row ? value.y : value.x;
}

Vector2 axes(float main, float cross, FlexDirection direction) {
    return direction == FlexDirection::Row ? Vector2 {main, cross} :
                                             Vector2 {cross, main};
}

float main_start(const ResolvedEdges& edges, FlexDirection direction) {
    return direction == FlexDirection::Row ? edges.left : edges.top;
}

float main_end(const ResolvedEdges& edges, FlexDirection direction) {
    return direction == FlexDirection::Row ? edges.right : edges.bottom;
}

float cross_start(const ResolvedEdges& edges, FlexDirection direction) {
    return direction == FlexDirection::Row ? edges.top : edges.left;
}

float cross_end(const ResolvedEdges& edges, FlexDirection direction) {
    return direction == FlexDirection::Row ? edges.bottom : edges.right;
}

} // namespace

void Surface::clear() {
    m_entries.clear();
}

void Surface::upsert(
    Entity entity,
    const Node& node,
    ContentSize content_size,
    BorderRadius border_radius,
    ScrollPosition scroll_position
) {
    auto& entry = m_entries[entity];
    entry.node = node;
    entry.content_size = std::move(content_size);
    entry.border_radius = border_radius;
    entry.scroll_position = scroll_position;
}

void Surface::upsert(
    Entity entity,
    const Node& node,
    Vector2 content_size,
    BorderRadius border_radius,
    ScrollPosition scroll_position
) {
    upsert(
        entity,
        node,
        ContentSize {
            .measure =
                FixedMeasure {
                    .size = content_size,
                    .preserve_aspect_ratio = true,
                },
        },
        border_radius,
        scroll_position
    );
}

void Surface::set_children(Entity entity, std::span<const Entity> children) {
    auto item = m_entries.find(entity);
    if (item == m_entries.end()) {
        return;
    }
    item->second.children.assign(children.begin(), children.end());
}

void Surface::remove(Entity entity) {
    m_entries.erase(entity);
}

bool Surface::contains(Entity entity) const {
    return m_entries.contains(entity);
}

const ComputedNode* Surface::get(Entity entity) const {
    const auto item = m_entries.find(entity);
    return item == m_entries.end() ? nullptr : &item->second.computed;
}

void Surface::compute(Entity root, Vector2 viewport_size) {
    auto item = m_entries.find(root);
    if (item == m_entries.end()) {
        return;
    }

    auto& node = item->second.node;
    Vector2 size {
        resolve(node.width, viewport_size.x, viewport_size.x),
        resolve(node.height, viewport_size.y, viewport_size.y),
    };
    size.x = clamp_dimension(
        size.x,
        node.min_width,
        node.max_width,
        viewport_size.x
    );
    size.y = clamp_dimension(
        size.y,
        node.min_height,
        node.max_height,
        viewport_size.y
    );
    layout_node(root, Vector2::Zero, size);
}

void Surface::layout_node(Entity entity, Vector2 position, Vector2 size) {
    auto item = m_entries.find(entity);
    if (item == m_entries.end()) {
        return;
    }

    auto& entry = item->second;
    const auto& node = entry.node;
    if (node.display == Display::None) {
        entry.computed = {};
        return;
    }

    auto border = resolve_edges(node.border, size);
    border.left = std::max(border.left, 0.0f);
    border.right = std::max(border.right, 0.0f);
    border.top = std::max(border.top, 0.0f);
    border.bottom = std::max(border.bottom, 0.0f);
    const auto padding = resolve_edges(node.padding, size);
    const Vector2 content_position {
        position.x + border.left + padding.left,
        position.y + border.top + padding.top,
    };
    const Vector2 content_size {
        std::max(
            0.0f,
            size.x - border.left - border.right - padding.left - padding.right
        ),
        std::max(
            0.0f,
            size.y - border.top - border.bottom - padding.top - padding.bottom
        ),
    };
    const auto radius_reference = std::min(size.x, size.y);
    const auto resolve_radius = [&](Length value) {
        return std::clamp(
            resolve(value, radius_reference),
            0.0f,
            radius_reference * 0.5f
        );
    };
    entry.computed = {
        .position = position,
        .size = size,
        .content_size = content_size,
        .content_position = content_position,
        .border =
            {
                .left = border.left,
                .top = border.top,
                .right = border.right,
                .bottom = border.bottom,
            },
        .border_radius = {
            .top_left = resolve_radius(entry.border_radius.top_left),
            .top_right = resolve_radius(entry.border_radius.top_right),
            .bottom_right = resolve_radius(entry.border_radius.bottom_right),
            .bottom_left = resolve_radius(entry.border_radius.bottom_left),
        },
    };

    struct FlowItem {
        Entity entity;
        ResolvedEdges margin;
        float main_size {0.0f};
        float cross_size {0.0f};
        float grow {0.0f};
        float shrink {0.0f};
        bool auto_cross {false};
    };

    const auto direction = node.flex_direction;
    const float available_main = main_axis(content_size, direction);
    const float available_cross = cross_axis(content_size, direction);
    std::vector<FlowItem> flow;
    std::vector<Entity> absolute;
    flow.reserve(entry.children.size());
    absolute.reserve(entry.children.size());

    for (const auto child_entity : entry.children) {
        const auto child_item = m_entries.find(child_entity);
        if (child_item == m_entries.end() ||
            child_item->second.node.display == Display::None) {
            continue;
        }
        const auto& child = child_item->second.node;
        if (child.position_type == PositionType::Absolute) {
            absolute.push_back(child_entity);
            continue;
        }

        const auto margin = resolve_edges(child.margin, content_size);
        const float child_cross_available = std::max(
            0.0f,
            available_cross - cross_start(margin, direction) -
                cross_end(margin, direction)
        );
        Optional<float> known_width;
        Optional<float> known_height;
        if (!child.width.is_auto()) {
            known_width = resolve(child.width, content_size.x);
        }
        if (!child.height.is_auto()) {
            known_height = resolve(child.height, content_size.y);
        }
        if (node.align_items == AlignItems::Stretch) {
            if (direction == FlexDirection::Column && !known_width) {
                known_width = child_cross_available;
            } else if (direction == FlexDirection::Row && !known_height) {
                known_height = child_cross_available;
            }
        }
        const auto intrinsic = child_item->second.content_size.compute(
            MeasureArgs {
                .available_width = AvailableSpace::max_content(),
                .available_height = AvailableSpace::max_content(),
            }
        );
        auto measured_size = child_item->second.content_size.compute(
            MeasureArgs {
                .known_width = known_width,
                .known_height = known_height,
                .available_width = AvailableSpace::definite(content_size.x),
                .available_height = AvailableSpace::definite(content_size.y),
            }
        );
        const auto* fixed_measure =
            std::get_if<FixedMeasure>(&child_item->second.content_size.measure);
        if (fixed_measure != nullptr && fixed_measure->preserve_aspect_ratio) {
            if (!child.width.is_auto() && child.height.is_auto() &&
                known_width && intrinsic.x > 0.0f) {
                measured_size.y = *known_width * intrinsic.y / intrinsic.x;
            } else if (
                !child.height.is_auto() && child.width.is_auto() &&
                known_height && intrinsic.y > 0.0f
            ) {
                measured_size.x = *known_height * intrinsic.x / intrinsic.y;
            }
        }
        const auto main_length =
            direction == FlexDirection::Row ? child.width : child.height;
        const auto cross_length =
            direction == FlexDirection::Row ? child.height : child.width;
        const auto basis =
            child.flex_basis.is_auto() ? main_length : child.flex_basis;
        flow.push_back(
            FlowItem {
                .entity = child_entity,
                .margin = margin,
                .main_size = resolve(
                    basis,
                    available_main,
                    main_axis(measured_size, direction)
                ),
                .cross_size = resolve(
                    cross_length,
                    available_cross,
                    cross_axis(measured_size, direction)
                ),
                .grow = std::max(0.0f, child.flex_grow),
                .shrink = std::max(0.0f, child.flex_shrink),
                .auto_cross = cross_length.is_auto(),
            }
        );
    }

    const float base_gap = resolve(node.gap, available_main);
    const float gap_count =
        flow.empty() ? 0.0f : static_cast<float>(flow.size() - 1);
    float occupied = base_gap * gap_count;
    float total_grow = 0.0f;
    float total_shrink_weight = 0.0f;
    for (const auto& child : flow) {
        occupied += child.main_size + main_start(child.margin, direction) +
                    main_end(child.margin, direction);
        total_grow += child.grow;
        total_shrink_weight += child.shrink * child.main_size;
    }

    const float remaining = available_main - occupied;
    if (remaining > 0.0f && total_grow > 0.0f) {
        for (auto& child : flow) {
            child.main_size += remaining * child.grow / total_grow;
        }
        occupied = available_main;
    } else if (remaining < 0.0f && total_shrink_weight > 0.0f) {
        for (auto& child : flow) {
            const float weight = child.shrink * child.main_size;
            child.main_size = std::max(
                0.0f,
                child.main_size + remaining * weight / total_shrink_weight
            );
        }
        occupied = available_main;
    }

    float cursor = 0.0f;
    float gap = base_gap;
    const float free_space = std::max(0.0f, available_main - occupied);
    switch (node.justify_content) {
        case JustifyContent::Start:
            break;
        case JustifyContent::Center:
            cursor = free_space * 0.5f;
            break;
        case JustifyContent::End:
            cursor = free_space;
            break;
        case JustifyContent::SpaceBetween:
            if (flow.size() > 1) {
                gap += free_space / static_cast<float>(flow.size() - 1);
            }
            break;
    }

    for (auto& child : flow) {
        auto& child_node = m_entries.at(child.entity).node;
        const float cross_available = std::max(
            0.0f,
            available_cross - cross_start(child.margin, direction) -
                cross_end(child.margin, direction)
        );
        if (child.auto_cross && node.align_items == AlignItems::Stretch) {
            child.cross_size = cross_available;
        } else if (
            direction == FlexDirection::Row && child.auto_cross &&
            child.main_size > 0.0f &&
            std::holds_alternative<TextMeasure>(
                m_entries.at(child.entity).content_size.measure
            )
        ) {
            const auto measured =
                m_entries.at(child.entity)
                    .content_size.compute(
                        MeasureArgs {
                            .known_width = child.main_size,
                            .available_width =
                                AvailableSpace::definite(child.main_size),
                            .available_height =
                                AvailableSpace::definite(available_cross),
                        }
                    );
            child.cross_size = measured.y;
        }

        const auto child_reference =
            axes(available_main, available_cross, direction);
        Vector2 child_size = axes(child.main_size, child.cross_size, direction);
        child_size.x = clamp_dimension(
            child_size.x,
            child_node.min_width,
            child_node.max_width,
            child_reference.x
        );
        child_size.y = clamp_dimension(
            child_size.y,
            child_node.min_height,
            child_node.max_height,
            child_reference.y
        );

        cursor += main_start(child.margin, direction);
        float cross_position = cross_start(child.margin, direction);
        switch (node.align_items) {
            case AlignItems::Start:
            case AlignItems::Stretch:
                break;
            case AlignItems::Center:
                cross_position += (cross_available - child.cross_size) * 0.5f;
                break;
            case AlignItems::End:
                cross_position += cross_available - child.cross_size;
                break;
        }

        Vector2 child_position =
            content_position + axes(cursor, cross_position, direction);
        child_position.x += resolve(child_node.left, content_size.x) -
                            resolve(child_node.right, content_size.x);
        child_position.y += resolve(child_node.top, content_size.y) -
                            resolve(child_node.bottom, content_size.y);
        layout_node(child.entity, child_position, child_size);

        cursor += child.main_size + main_end(child.margin, direction) + gap;
    }

    for (const auto child_entity : absolute) {
        const auto& child_entry = m_entries.at(child_entity);
        const auto& child = child_entry.node;
        const auto margin = resolve_edges(child.margin, content_size);
        const float left = resolve(child.left, content_size.x);
        const float right = resolve(child.right, content_size.x);
        const float top = resolve(child.top, content_size.y);
        const float bottom = resolve(child.bottom, content_size.y);

        Optional<float> known_width;
        Optional<float> known_height;
        if (!child.width.is_auto()) {
            known_width = resolve(child.width, content_size.x);
        } else if (!child.left.is_auto() && !child.right.is_auto()) {
            known_width = std::max(0.0f, content_size.x - left - right);
        }
        if (!child.height.is_auto()) {
            known_height = resolve(child.height, content_size.y);
        } else if (!child.top.is_auto() && !child.bottom.is_auto()) {
            known_height = std::max(0.0f, content_size.y - top - bottom);
        }
        const auto intrinsic = child_entry.content_size.compute(
            MeasureArgs {
                .available_width = AvailableSpace::max_content(),
                .available_height = AvailableSpace::max_content(),
            }
        );
        auto measured_size = child_entry.content_size.compute(
            MeasureArgs {
                .known_width = known_width,
                .known_height = known_height,
                .available_width = AvailableSpace::definite(content_size.x),
                .available_height = AvailableSpace::definite(content_size.y),
            }
        );
        const auto* fixed_measure =
            std::get_if<FixedMeasure>(&child_entry.content_size.measure);
        if (fixed_measure != nullptr && fixed_measure->preserve_aspect_ratio) {
            if (!child.width.is_auto() && child.height.is_auto() &&
                known_width && intrinsic.x > 0.0f) {
                measured_size.y = *known_width * intrinsic.y / intrinsic.x;
            } else if (
                !child.height.is_auto() && child.width.is_auto() &&
                known_height && intrinsic.y > 0.0f
            ) {
                measured_size.x = *known_height * intrinsic.x / intrinsic.y;
            }
        }
        Vector2 child_size {
            resolve(
                child.width,
                content_size.x,
                child.left.is_auto() || child.right.is_auto() ?
                    measured_size.x :
                    content_size.x - left - right
            ),
            resolve(
                child.height,
                content_size.y,
                child.top.is_auto() || child.bottom.is_auto() ?
                    measured_size.y :
                    content_size.y - top - bottom
            ),
        };
        child_size.x = clamp_dimension(
            child_size.x,
            child.min_width,
            child.max_width,
            content_size.x
        );
        child_size.y = clamp_dimension(
            child_size.y,
            child.min_height,
            child.max_height,
            content_size.y
        );

        Vector2 child_position = content_position;
        child_position.x +=
            !child.left.is_auto() ?
                left + margin.left :
                content_size.x - right - child_size.x - margin.right;
        child_position.y +=
            !child.top.is_auto() ?
                top + margin.top :
                content_size.y - bottom - child_size.y - margin.bottom;
        layout_node(child_entity, child_position, child_size);
    }

    Vector2 scroll_content_size = content_size;
    for (const auto child_entity : entry.children) {
        const auto child_item = m_entries.find(child_entity);
        if (child_item == m_entries.end() ||
            child_item->second.node.display == Display::None) {
            continue;
        }
        const auto& child = child_item->second.computed;
        scroll_content_size.x = std::max(
            scroll_content_size.x,
            child.position.x + child.size.x - content_position.x
        );
        scroll_content_size.y = std::max(
            scroll_content_size.y,
            child.position.y + child.size.y - content_position.y
        );
    }
    entry.computed.scroll_content_size = scroll_content_size;

    const Vector2 max_scroll {
        std::max(0.0f, scroll_content_size.x - content_size.x),
        std::max(0.0f, scroll_content_size.y - content_size.y),
    };
    Vector2 scroll_position {
        node.overflow.x == OverflowAxis::Scroll ?
            std::clamp(entry.scroll_position.offset.x, 0.0f, max_scroll.x) :
            0.0f,
        node.overflow.y == OverflowAxis::Scroll ?
            std::clamp(entry.scroll_position.offset.y, 0.0f, max_scroll.y) :
            0.0f,
    };
    entry.computed.scroll_position = scroll_position;
    if (scroll_position != Vector2::Zero) {
        for (const auto child_entity : entry.children) {
            translate_subtree(
                child_entity,
                {-scroll_position.x, -scroll_position.y}
            );
        }
    }
}

void Surface::translate_subtree(Entity entity, Vector2 offset) {
    const auto item = m_entries.find(entity);
    if (item == m_entries.end()) {
        return;
    }
    item->second.computed.position += offset;
    item->second.computed.content_position += offset;
    for (const auto child : item->second.children) {
        translate_subtree(child, offset);
    }
}

} // namespace ets::ui
