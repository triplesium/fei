#include "ui_widgets/plugin.hpp"

#include <algorithm>
#include <vector>

namespace fei::ui_widgets {

namespace {

std::size_t depth(Entity entity, const Query<Entity, const ChildOf>& parents) {
    std::size_t result = 0;
    while (const auto parent = parents.get(entity)) {
        ++result;
        entity = std::get<1>(*parent).parent;
    }
    return result;
}

float scroll_axis(float position, float delta, float content, float visible) {
    return std::clamp(
        position + delta,
        0.0f,
        std::max(0.0f, content - visible)
    );
}

} // namespace

void sync_scroll_areas(
    Query<Entity, const ScrollArea>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const ScrollArea>::Filter<Without<ui::ScrollPosition>>
        missing_scroll_positions,
    Query<Entity, const ScrollArea>::Filter<Without<ui::RelativeCursorPosition>>
        missing_cursor_positions,
    Commands commands
) {
    for (const auto& [entity, area] : missing_nodes) {
        (void)area;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, area] : missing_scroll_positions) {
        (void)area;
        commands.entity(entity).add(ui::ScrollPosition {});
    }
    for (const auto& [entity, area] : missing_cursor_positions) {
        (void)area;
        commands.entity(entity).add(ui::RelativeCursorPosition {});
    }
}

void update_scroll_areas(
    Query<
        Entity,
        const ScrollArea,
        const ui::Node,
        ui::ScrollPosition,
        const ui::ComputedNode,
        const ui::RelativeCursorPosition> areas,
    Query<Entity, const ui::ComputedNode> computed_nodes,
    Query<Entity, const ChildOf> parents,
    ResRO<MouseScrollInput> mouse_scroll,
    EventReader<ScrollIntoView> scroll_into_view
) {
    while (const auto event = scroll_into_view.next()) {
        const auto target = computed_nodes.get(event->entity);
        if (!target) {
            continue;
        }
        auto ancestor = event->entity;
        while (const auto parent = parents.get(ancestor)) {
            ancestor = std::get<1>(*parent).parent;
            const auto area = areas.get(ancestor);
            if (!area) {
                continue;
            }
            const auto& node = std::get<2>(*area);
            auto position = std::get<3>(*area);
            const auto& computed = std::get<4>(*area);
            const auto& target_node = std::get<1>(*target);
            auto next = position.read().offset;
            if (node.overflow.x == ui::OverflowAxis::Scroll) {
                if (target_node.position.x < computed.content_position.x) {
                    next.x +=
                        target_node.position.x - computed.content_position.x;
                } else if (
                    target_node.position.x + target_node.size.x >
                    computed.content_position.x + computed.content_size.x
                ) {
                    next.x += target_node.position.x + target_node.size.x -
                              computed.content_position.x -
                              computed.content_size.x;
                }
                next.x = scroll_axis(
                    0.0f,
                    next.x,
                    computed.scroll_content_size.x,
                    computed.content_size.x
                );
            }
            if (node.overflow.y == ui::OverflowAxis::Scroll) {
                if (target_node.position.y < computed.content_position.y) {
                    next.y +=
                        target_node.position.y - computed.content_position.y;
                } else if (
                    target_node.position.y + target_node.size.y >
                    computed.content_position.y + computed.content_size.y
                ) {
                    next.y += target_node.position.y + target_node.size.y -
                              computed.content_position.y -
                              computed.content_size.y;
                }
                next.y = scroll_axis(
                    0.0f,
                    next.y,
                    computed.scroll_content_size.y,
                    computed.content_size.y
                );
            }
            if (position.read().offset != next) {
                position = ui::ScrollPosition {.offset = next};
            }
            break;
        }
    }

    Vector2 remaining = mouse_scroll->delta() * -20.0f;
    if (remaining == Vector2::Zero) {
        return;
    }

    std::vector<Entity> hovered;
    for (const auto& [entity, area, node, position, computed, cursor] : areas) {
        (void)area;
        (void)node;
        (void)position;
        (void)computed;
        if (cursor.is_cursor_over()) {
            hovered.push_back(entity);
        }
    }
    std::ranges::sort(hovered, [&](Entity lhs, Entity rhs) {
        return depth(lhs, parents) > depth(rhs, parents);
    });

    for (const auto entity : hovered) {
        const auto area = areas.get(entity);
        const auto& node = std::get<2>(*area);
        auto position = std::get<3>(*area);
        const auto& computed = std::get<4>(*area);
        auto next = position.read().offset;
        if (node.overflow.x == ui::OverflowAxis::Scroll) {
            const auto value = scroll_axis(
                next.x,
                remaining.x,
                computed.scroll_content_size.x,
                computed.content_size.x
            );
            remaining.x -= value - next.x;
            next.x = value;
        }
        if (node.overflow.y == ui::OverflowAxis::Scroll) {
            const auto value = scroll_axis(
                next.y,
                remaining.y,
                computed.scroll_content_size.y,
                computed.content_size.y
            );
            remaining.y -= value - next.y;
            next.y = value;
        }
        if (position.read().offset != next) {
            position = ui::ScrollPosition {.offset = next};
        }
        if (remaining == Vector2::Zero) {
            break;
        }
    }
}

} // namespace fei::ui_widgets
