#include "ui_widgets/plugin.hpp"

#include <algorithm>

namespace fei::ui_widgets {

namespace {

Vector2 aligned_position(
    const ui::ComputedNode& anchor,
    Vector2 popup_size,
    const PopoverPlacement& placement,
    PopoverSide side
) {
    Vector2 result;
    if (side == PopoverSide::Bottom || side == PopoverSide::Top) {
        switch (placement.align) {
            case PopoverAlign::Start:
                result.x = anchor.position.x;
                break;
            case PopoverAlign::Center:
                result.x =
                    anchor.position.x + (anchor.size.x - popup_size.x) * 0.5f;
                break;
            case PopoverAlign::End:
                result.x = anchor.position.x + anchor.size.x - popup_size.x;
                break;
        }
        result.y = side == PopoverSide::Bottom ?
                       anchor.position.y + anchor.size.y + placement.gap :
                       anchor.position.y - popup_size.y - placement.gap;
    } else {
        switch (placement.align) {
            case PopoverAlign::Start:
                result.y = anchor.position.y;
                break;
            case PopoverAlign::Center:
                result.y =
                    anchor.position.y + (anchor.size.y - popup_size.y) * 0.5f;
                break;
            case PopoverAlign::End:
                result.y = anchor.position.y + anchor.size.y - popup_size.y;
                break;
        }
        result.x = side == PopoverSide::Right ?
                       anchor.position.x + anchor.size.x + placement.gap :
                       anchor.position.x - popup_size.x - placement.gap;
    }
    return result;
}

PopoverSide opposite(PopoverSide side) {
    switch (side) {
        case PopoverSide::Bottom:
            return PopoverSide::Top;
        case PopoverSide::Top:
            return PopoverSide::Bottom;
        case PopoverSide::Right:
            return PopoverSide::Left;
        case PopoverSide::Left:
            return PopoverSide::Right;
    }
    return side;
}

bool primary_overflow(
    Vector2 position,
    Vector2 size,
    PopoverSide side,
    Vector2 viewport_min,
    Vector2 viewport_max
) {
    switch (side) {
        case PopoverSide::Bottom:
            return position.y + size.y > viewport_max.y;
        case PopoverSide::Top:
            return position.y < viewport_min.y;
        case PopoverSide::Right:
            return position.x + size.x > viewport_max.x;
        case PopoverSide::Left:
            return position.x < viewport_min.x;
    }
    return false;
}

} // namespace

void sync_popovers(
    Query<Entity, const Popover>::Filter<Without<ui::Node>> missing_nodes,
    Commands commands
) {
    for (const auto& [entity, popover] : missing_nodes) {
        (void)popover;
        commands.add_command([entity](World& world) {
            if (!world.has_component<ui::Node>(entity)) {
                world.add_component(
                    entity,
                    ui::Node {.position_type = ui::PositionType::Absolute}
                );
            }
        });
    }
}

void update_popovers(
    Query<Entity, const Popover, ui::Node, const ui::ComputedNode> popovers,
    Query<Entity, const ui::ComputedNode> computed_nodes,
    Query<Entity, const ChildOf> parents,
    ResRO<Window> window
) {
    for (auto [entity, popover, node, popup_computed] : popovers) {
        if (node.read().display == ui::Display::None) {
            continue;
        }
        const auto anchor_item = computed_nodes.get(popover.anchor);
        if (!anchor_item) {
            continue;
        }
        const auto& anchor = std::get<1>(*anchor_item);
        const auto margin = std::max(0.0f, popover.placement.viewport_margin);
        const Vector2 viewport_min {margin, margin};
        const Vector2 viewport_max {
            std::max(margin, static_cast<float>(window->width) - margin),
            std::max(margin, static_cast<float>(window->height) - margin),
        };
        Vector2 popup_size = popup_computed.size;
        if (popup_size.x <= 0.0f && !node.read().width.is_auto()) {
            popup_size.x = node.read().width.value;
        }
        if (popup_size.y <= 0.0f && !node.read().height.is_auto()) {
            popup_size.y = node.read().height.value;
        }

        auto side = popover.placement.side;
        auto position =
            aligned_position(anchor, popup_size, popover.placement, side);
        if (popover.placement.flip && primary_overflow(
                                          position,
                                          popup_size,
                                          side,
                                          viewport_min,
                                          viewport_max
                                      )) {
            const auto fallback = opposite(side);
            const auto fallback_position = aligned_position(
                anchor,
                popup_size,
                popover.placement,
                fallback
            );
            if (!primary_overflow(
                    fallback_position,
                    popup_size,
                    fallback,
                    viewport_min,
                    viewport_max
                )) {
                side = fallback;
                position = fallback_position;
            }
        }
        (void)side;
        position.x = std::clamp(
            position.x,
            viewport_min.x,
            std::max(viewport_min.x, viewport_max.x - popup_size.x)
        );
        position.y = std::clamp(
            position.y,
            viewport_min.y,
            std::max(viewport_min.y, viewport_max.y - popup_size.y)
        );

        Vector2 parent_origin;
        if (const auto parent = parents.get(entity)) {
            if (const auto parent_computed =
                    computed_nodes.get(std::get<1>(*parent).parent)) {
                parent_origin = std::get<1>(*parent_computed).content_position;
            }
        }
        auto next = node.read();
        next.position_type = ui::PositionType::Absolute;
        next.left = ui::px(position.x - parent_origin.x);
        next.top = ui::px(position.y - parent_origin.y);
        next.right = ui::Length::automatic();
        next.bottom = ui::Length::automatic();
        if (next.left != node.read().left || next.top != node.read().top ||
            next.right != node.read().right ||
            next.bottom != node.read().bottom ||
            next.position_type != node.read().position_type) {
            node = next;
        }
    }
}

} // namespace fei::ui_widgets
