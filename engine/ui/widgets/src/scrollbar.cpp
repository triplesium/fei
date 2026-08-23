#include "ui_widgets/plugin.hpp"

#include <algorithm>
#include <functional>

namespace ets::ui_widgets {

namespace {

Optional<Entity> first_thumb(
    Entity root,
    const Query<Entity, const ScrollbarThumb>& thumbs,
    const Query<Entity, const Children>& child_lists
) {
    Optional<Entity> result;
    std::function<void(Entity)> visit = [&](Entity entity) {
        if (result) {
            return;
        }
        if (entity != root && thumbs.get(entity)) {
            result = entity;
            return;
        }
        if (const auto children = child_lists.get(entity)) {
            for (const auto child : std::get<1>(*children)) {
                visit(child);
            }
        }
    };
    visit(root);
    return result;
}

float axis(Vector2 value, ControlOrientation orientation) {
    return orientation == ControlOrientation::Vertical ? value.y : value.x;
}

} // namespace

void sync_scrollbars(
    Query<Entity, const Scrollbar>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Scrollbar>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Scrollbar>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ui::Node>>
        missing_thumb_nodes,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ui::Interaction>>
        missing_thumb_interactions,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ui::FocusPolicy>>
        missing_thumb_policies,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ScrollbarDragState>>
        missing_drag_states,
    Commands commands
) {
    for (const auto& [entity, scrollbar] : missing_nodes) {
        (void)scrollbar;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, scrollbar] : missing_interactions) {
        (void)scrollbar;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, scrollbar] : missing_policies) {
        (void)scrollbar;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
    for (const auto& [entity, thumb] : missing_thumb_nodes) {
        (void)thumb;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, thumb] : missing_thumb_interactions) {
        (void)thumb;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, thumb] : missing_thumb_policies) {
        (void)thumb;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
    for (const auto& [entity, thumb] : missing_drag_states) {
        (void)thumb;
        commands.entity(entity).add(ScrollbarDragState {});
    }
}

void update_scrollbars(
    Query<
        Entity,
        const Scrollbar,
        const ui::Interaction,
        const ui::ComputedNode> scrollbars,
    Query<Entity, const ScrollbarThumb> thumbs,
    Query<
        Entity,
        ui::Node,
        const ui::Interaction,
        const ui::ComputedNode,
        ScrollbarDragState> thumb_nodes,
    Query<Entity, const Children> child_lists,
    Query<Entity, ui::ScrollPosition, const ui::ComputedNode> targets,
    ResRO<MouseInput> mouse,
    Commands commands
) {
    for (const auto& [entity, scrollbar, interaction, computed] : scrollbars) {
        const auto thumb_entity = first_thumb(entity, thumbs, child_lists);
        const auto target = targets.get(scrollbar.target);
        if (!thumb_entity || !target) {
            continue;
        }
        const auto thumb = thumb_nodes.get(*thumb_entity);
        if (!thumb) {
            continue;
        }

        auto thumb_node = std::get<1>(*thumb);
        const auto thumb_interaction = std::get<2>(*thumb);
        auto drag = std::get<4>(*thumb);
        auto scroll_position = std::get<1>(*target);
        const auto& target_computed = std::get<2>(*target);

        const auto track_length =
            std::max(0.0f, axis(computed.size, scrollbar.orientation));
        const auto visible_length = std::max(
            0.0f,
            axis(target_computed.content_size, scrollbar.orientation)
        );
        const auto content_length = std::max(
            visible_length,
            axis(target_computed.scroll_content_size, scrollbar.orientation)
        );
        const auto scroll_range =
            std::max(0.0f, content_length - visible_length);
        const auto minimum =
            std::clamp(scrollbar.min_thumb_length, 0.0f, track_length);
        const auto proportional =
            content_length > 0.0f ?
                track_length * visible_length / content_length :
                track_length;
        const auto thumb_length =
            std::clamp(proportional, minimum, track_length);
        const auto travel = std::max(0.0f, track_length - thumb_length);
        const auto current_offset =
            axis(scroll_position.read().offset, scrollbar.orientation);
        const auto thumb_offset =
            scroll_range > 0.0f ? travel * current_offset / scroll_range : 0.0f;

        thumb_node->position_type = ui::PositionType::Absolute;
        if (scrollbar.orientation == ControlOrientation::Vertical) {
            thumb_node->height = ui::px(thumb_length);
            thumb_node->top = ui::px(thumb_offset);
        } else {
            thumb_node->width = ui::px(thumb_length);
            thumb_node->left = ui::px(thumb_offset);
        }

        if (mouse->just_pressed(MouseButton::Left) &&
            thumb_interaction == ui::Interaction::Pressed) {
            drag = ScrollbarDragState {
                .dragging = true,
                .offset = current_offset,
                .pointer_start = axis(mouse->position(), scrollbar.orientation),
                .pointer_position =
                    axis(mouse->position(), scrollbar.orientation),
            };
            commands.entity(*thumb_entity).add(ui::Pressed {});
            continue;
        }

        if (mouse->just_pressed(MouseButton::Left) &&
            interaction == ui::Interaction::Pressed) {
            const auto track_start =
                axis(computed.position, scrollbar.orientation);
            const auto pointer = axis(mouse->position(), scrollbar.orientation);
            const auto click_position =
                track_length > 0.0f ?
                    (pointer - track_start) * content_length / track_length :
                    0.0f;
            auto offset = scroll_position.read().offset;
            auto next = current_offset + (click_position > current_offset ?
                                              visible_length :
                                              -visible_length);
            next = std::clamp(next, 0.0f, scroll_range);
            if (scrollbar.orientation == ControlOrientation::Vertical) {
                offset.y = next;
            } else {
                offset.x = next;
            }
            scroll_position = ui::ScrollPosition {.offset = offset};
            continue;
        }

        if (!drag->dragging) {
            continue;
        }

        const auto pointer = axis(mouse->position(), scrollbar.orientation);
        auto next = drag->offset;
        if (track_length > 0.0f) {
            next +=
                (pointer - drag->pointer_start) * content_length / track_length;
        }
        next = std::clamp(next, 0.0f, scroll_range);

        if (mouse->just_released(MouseButton::Left)) {
            auto offset = scroll_position.read().offset;
            if (scrollbar.orientation == ControlOrientation::Vertical) {
                offset.y = next;
            } else {
                offset.x = next;
            }
            scroll_position = ui::ScrollPosition {.offset = offset};
            drag = ScrollbarDragState {};
            commands.entity(*thumb_entity).remove<ui::Pressed>();
        } else if (
            mouse->pressed(MouseButton::Left) &&
            pointer != drag->pointer_position
        ) {
            auto offset = scroll_position.read().offset;
            if (scrollbar.orientation == ControlOrientation::Vertical) {
                offset.y = next;
            } else {
                offset.x = next;
            }
            scroll_position = ui::ScrollPosition {.offset = offset};
            auto state = drag.read();
            state.pointer_position = pointer;
            drag = state;
        }
    }
}

} // namespace ets::ui_widgets
