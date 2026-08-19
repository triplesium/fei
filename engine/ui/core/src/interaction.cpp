#include "ui/plugin.hpp"
#include "window/window.hpp"

#include <unordered_set>
#include <vector>

namespace fei::ui {

namespace {

bool contains(Rect bounds, Vector2 point) {
    return point.x >= bounds.min.x && point.x <= bounds.max.x &&
           point.y >= bounds.min.y && point.y <= bounds.max.y;
}

Optional<Vector2>
normalized_cursor(const ComputedNode& computed, Vector2 cursor) {
    if (computed.size.x <= 0.0f || computed.size.y <= 0.0f) {
        return nullopt;
    }
    return Vector2 {
        (cursor.x - computed.position.x) / computed.size.x - 0.5f,
        (cursor.y - computed.position.y) / computed.size.y - 0.5f,
    };
}

bool cursor_over_node(
    const Node& node,
    const ComputedNode& computed,
    Optional<Rect> clip,
    Vector2 cursor
) {
    if (node.display == Display::None || computed.size.x <= 0.0f ||
        computed.size.y <= 0.0f) {
        return false;
    }
    const Rect bounds {
        .min = computed.position,
        .max = computed.position + computed.size,
    };
    return contains(bounds, cursor) && (!clip || contains(*clip, cursor));
}

void set_interaction(ComponentRW<Interaction> interaction, Interaction value) {
    if (interaction.read() != value) {
        interaction = value;
    }
}

} // namespace

void update_interactions(
    Query<Entity, const Node, const ComputedNode> nodes,
    Query<Entity, Interaction> interactions,
    Query<Entity, RelativeCursorPosition> cursor_positions,
    Query<Entity, const FocusPolicy> focus_policies,
    Query<Entity, const InteractionDisabled> disabled,
    Query<Entity, const input_focus::TabIndex> tab_indices,
    Query<Entity, const ChildOf> parents,
    Query<Entity, const CalculatedClip> calculated_clips,
    ResRO<Stack> stack,
    ResRO<MouseInput> mouse,
    ResRO<Window> window,
    ResRW<input_focus::InputFocus> input_focus,
    ResRW<input_focus::InputFocusVisible> input_focus_visible
) {
    Optional<Vector2> cursor;
    const auto mouse_position = mouse->position();
    if (mouse_position.x >= 0.0f && mouse_position.y >= 0.0f &&
        mouse_position.x <= static_cast<float>(window->width) &&
        mouse_position.y <= static_cast<float>(window->height)) {
        cursor = mouse_position;
    }

    const auto clip_for = [&](Entity entity) -> Optional<Rect> {
        const auto item = calculated_clips.get(entity);
        return item ? Optional<Rect> {std::get<1>(*item).clip} : nullopt;
    };
    const auto is_over = [&](Entity entity, Vector2 point) {
        const auto item = nodes.get(entity);
        return item && cursor_over_node(
                           std::get<1>(*item),
                           std::get<2>(*item),
                           clip_for(entity),
                           point
                       );
    };

    for (auto [entity, position] : cursor_positions) {
        RelativeCursorPosition next;
        if (cursor) {
            if (const auto item = nodes.get(entity)) {
                const auto& node = std::get<1>(*item);
                const auto& computed = std::get<2>(*item);
                if (node.display != Display::None) {
                    next.normalized = normalized_cursor(computed, *cursor);
                    next.cursor_over = is_over(entity, *cursor);
                }
            }
        }
        if (position.read() != next) {
            position = next;
        }
    }

    const std::unordered_set<Entity> stacked(
        stack->nodes.begin(),
        stack->nodes.end()
    );
    for (auto [entity, interaction] : interactions) {
        const auto node = nodes.get(entity);
        const bool valid = !disabled.get(entity) && node &&
                           stacked.contains(entity) && cursor &&
                           std::get<1>(*node).display != Display::None &&
                           normalized_cursor(std::get<2>(*node), *cursor);
        if (!valid) {
            set_interaction(interaction, Interaction::None);
        } else if (
            !is_over(entity, *cursor) &&
            interaction.read() == Interaction::Hovered
        ) {
            interaction = Interaction::None;
        }
    }

    std::vector<Entity> hovered;
    hovered.reserve(stack->nodes.size());
    if (cursor) {
        for (auto item = stack->nodes.rbegin(); item != stack->nodes.rend();
             ++item) {
            if (is_over(*item, *cursor)) {
                hovered.push_back(*item);
            }
        }
    }

    std::unordered_set<Entity> focused;
    focused.reserve(hovered.size());
    for (const auto entity : hovered) {
        focused.insert(entity);
        const auto policy = focus_policies.get(entity);
        if (!policy || std::get<1>(*policy) == FocusPolicy::Block) {
            break;
        }
    }

    if (mouse->just_released(MouseButton::Left)) {
        for (auto [entity, interaction] : interactions) {
            (void)entity;
            if (interaction.read() != Interaction::Pressed) {
                continue;
            }
            interaction = Interaction::None;
        }
    }

    const bool clicked = mouse->just_pressed(MouseButton::Left);
    if (clicked) {
        Optional<Entity> next_focus;
        for (const auto entity : hovered) {
            if (!focused.contains(entity)) {
                break;
            }
            auto current = entity;
            while (true) {
                if (tab_indices.get(current)) {
                    next_focus = current;
                    break;
                }
                const auto parent = parents.get(current);
                if (!parent) {
                    break;
                }
                current = std::get<1>(*parent).parent;
            }
            if (next_focus) {
                break;
            }
        }
        if (input_focus->get() != next_focus) {
            if (next_focus) {
                input_focus->set(*next_focus, input_focus::FocusCause::Pointer);
            } else {
                input_focus->clear(input_focus::FocusCause::Pointer);
            }
        }
        if (input_focus_visible->visible) {
            input_focus_visible->visible = false;
        }
    }

    bool blocked = false;
    for (const auto entity : hovered) {
        const auto interaction_item = interactions.get(entity);
        if (blocked) {
            if (interaction_item) {
                auto interaction = std::get<1>(*interaction_item);
                if (interaction.read() != Interaction::Pressed) {
                    set_interaction(interaction, Interaction::None);
                }
            }
            continue;
        }

        if (interaction_item) {
            auto interaction = std::get<1>(*interaction_item);
            if (disabled.get(entity)) {
                set_interaction(interaction, Interaction::None);
            } else if (clicked) {
                set_interaction(interaction, Interaction::Pressed);
            } else if (interaction.read() == Interaction::None) {
                interaction = Interaction::Hovered;
            }
        }

        const auto policy = focus_policies.get(entity);
        if (!policy || std::get<1>(*policy) == FocusPolicy::Block) {
            blocked = true;
        }
    }
}

} // namespace fei::ui
