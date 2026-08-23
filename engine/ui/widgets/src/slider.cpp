#include "ui_widgets/plugin.hpp"

#include <algorithm>
#include <functional>

namespace ets::ui_widgets {

namespace {

bool is_vertical(const Slider& slider, const ui::ComputedNode& node) {
    return slider.orientation == SliderOrientation::Vertical ||
           (slider.orientation == SliderOrientation::Auto &&
            node.size.y > node.size.x);
}

Optional<Entity> first_thumb(
    Entity root,
    const Query<Entity, const SliderThumb, const ui::ComputedNode>& thumbs,
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

float pointer_axis(Vector2 pointer, bool vertical) {
    return vertical ? pointer.y : pointer.x;
}

float value_from_click(
    Vector2 pointer,
    const ui::ComputedNode& node,
    const SliderRange& range,
    float thumb_size,
    bool vertical
) {
    const auto track_size = (vertical ? node.size.y : node.size.x) - thumb_size;
    if (track_size <= 0.0f) {
        return range.center();
    }
    const auto local = vertical ? node.position.y + node.size.y - pointer.y :
                                  pointer.x - node.position.x;
    return range.start() +
           (local - thumb_size * 0.5f) * range.span() / track_size;
}

bool contains(const ui::ComputedNode& node, Vector2 point) {
    return point.x >= node.position.x &&
           point.x <= node.position.x + node.size.x &&
           point.y >= node.position.y &&
           point.y <= node.position.y + node.size.y;
}

} // namespace

float SliderRange::clamp(float value) const {
    if (maximum <= minimum) {
        return minimum;
    }
    return std::clamp(value, minimum, maximum);
}

float SliderRange::thumb_position(float value) const {
    return maximum > minimum ? (value - minimum) / span() : 0.5f;
}

void sync_sliders(
    Query<Entity, const Slider>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Slider>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Slider>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const Slider>::Filter<Without<SliderValue>> missing_values,
    Query<Entity, const Slider>::Filter<Without<SliderRange>> missing_ranges,
    Query<Entity, const Slider>::Filter<Without<SliderStep>> missing_steps,
    Query<Entity, const Slider>::Filter<Without<SliderDragState>>
        missing_drag_states,
    Query<Entity, const SliderThumb>::Filter<Without<ui::FocusPolicy>>
        missing_thumb_policies,
    Commands commands
) {
    for (const auto& [entity, slider] : missing_nodes) {
        (void)slider;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, slider] : missing_interactions) {
        (void)slider;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, slider] : missing_policies) {
        (void)slider;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
    for (const auto& [entity, slider] : missing_values) {
        (void)slider;
        commands.entity(entity).add(SliderValue {});
    }
    for (const auto& [entity, slider] : missing_ranges) {
        (void)slider;
        commands.entity(entity).add(SliderRange {});
    }
    for (const auto& [entity, slider] : missing_steps) {
        (void)slider;
        commands.entity(entity).add(SliderStep {});
    }
    for (const auto& [entity, slider] : missing_drag_states) {
        (void)slider;
        commands.entity(entity).add(SliderDragState {});
    }
    for (const auto& [entity, thumb] : missing_thumb_policies) {
        (void)thumb;
        commands.entity(entity).add(ui::FocusPolicy::Pass);
    }
}

void update_sliders(
    Query<
        Entity,
        const Slider,
        const SliderValue,
        const SliderRange,
        const SliderStep,
        const ui::Interaction,
        SliderDragState> sliders,
    Query<Entity, const ui::ComputedNode> computed_nodes,
    Query<Entity, const SliderThumb, const ui::ComputedNode> thumbs,
    Query<Entity, const Children> child_lists,
    Query<Entity, const SliderPrecision> precisions,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventReader<SetSliderValue> set_values,
    Commands commands,
    EventWriter<ValueChange<float>> changed
) {
    const auto request = [&](Entity entity, SliderValueChange change) {
        const auto item = sliders.get(entity);
        if (!item) {
            return;
        }
        const auto& value = std::get<2>(*item);
        const auto& range = std::get<3>(*item);
        const auto& step = std::get<4>(*item);
        float next = value.value;
        switch (change.kind) {
            case SliderValueChangeKind::Absolute:
                next = change.value;
                break;
            case SliderValueChangeKind::Relative:
                next += change.value;
                break;
            case SliderValueChangeKind::RelativeStep:
                next += change.value * step.value;
                break;
        }
        changed.send(
            ValueChange<float> {
                .source = entity,
                .value = range.clamp(next),
                .is_final = true,
            }
        );
    };

    if (input_focus->get()) {
        const auto entity = *input_focus->get();
        const auto item = sliders.get(entity);
        if (item && !disabled.get(entity)) {
            const auto& slider = std::get<1>(*item);
            const auto& range = std::get<3>(*item);
            bool vertical = slider.orientation == SliderOrientation::Vertical;
            if (slider.orientation == SliderOrientation::Auto) {
                if (const auto computed = computed_nodes.get(entity)) {
                    const auto& node = std::get<1>(*computed);
                    vertical = node.size.y > node.size.x;
                }
            }

            if (keyboard->just_pressed(KeyCode::Home)) {
                request(entity, SliderValueChange::absolute(range.start()));
            } else if (keyboard->just_pressed(KeyCode::End)) {
                request(entity, SliderValueChange::absolute(range.end()));
            } else if (
                (!vertical && keyboard->just_pressed(KeyCode::Left)) ||
                (vertical && keyboard->just_pressed(KeyCode::Down))
            ) {
                request(entity, SliderValueChange::relative_step(-1.0f));
            } else if (
                (!vertical && keyboard->just_pressed(KeyCode::Right)) ||
                (vertical && keyboard->just_pressed(KeyCode::Up))
            ) {
                request(entity, SliderValueChange::relative_step(1.0f));
            }
        }
    }

    for (auto [entity, slider, value, range, step, interaction, drag] :
         sliders) {
        const auto computed = computed_nodes.get(entity);
        if (!computed) {
            continue;
        }
        const auto& node = std::get<1>(*computed);
        const bool vertical = is_vertical(slider, node);
        const auto thumb_entity = first_thumb(entity, thumbs, child_lists);
        float thumb_size = 0.0f;
        bool pointer_over_thumb = false;
        if (thumb_entity) {
            const auto thumb = thumbs.get(*thumb_entity);
            const auto& thumb_node = std::get<2>(*thumb);
            thumb_size = vertical ? thumb_node.size.y : thumb_node.size.x;
            pointer_over_thumb = contains(thumb_node, mouse->position());
        }

        if (disabled.get(entity)) {
            if (drag->dragging) {
                drag = SliderDragState {};
                commands.entity(entity).remove<ui::Pressed>();
            }
            continue;
        }

        if (mouse->just_pressed(MouseButton::Left) &&
            interaction == ui::Interaction::Pressed) {
            SliderDragState state {
                .dragging = true,
                .changed = false,
                .offset = value.value,
                .pointer_start = pointer_axis(mouse->position(), vertical),
                .pointer_position = pointer_axis(mouse->position(), vertical),
            };
            commands.entity(entity).add(ui::Pressed {});

            if (!pointer_over_thumb && slider.track_click != TrackClick::Drag) {
                auto next = value_from_click(
                    mouse->position(),
                    node,
                    range,
                    thumb_size,
                    vertical
                );
                if (slider.track_click == TrackClick::Step) {
                    next = next < value.value ? value.value - step.value :
                                                value.value + step.value;
                } else if (const auto precision = precisions.get(entity)) {
                    next = std::get<1>(*precision).round(next);
                }
                next = range.clamp(next);
                state.offset = next;
                state.changed = true;
                changed.send(
                    ValueChange<float> {
                        .source = entity,
                        .value = next,
                        .is_final = false,
                    }
                );
            }
            drag = state;
            continue;
        }

        if (!drag->dragging) {
            continue;
        }

        const auto current_pointer = pointer_axis(mouse->position(), vertical);
        const auto direction = vertical ? -1.0f : 1.0f;
        const auto travel =
            std::max(1.0f, (vertical ? node.size.y : node.size.x) - thumb_size);
        auto next = drag->offset + direction *
                                       (current_pointer - drag->pointer_start) *
                                       range.span() / travel;
        if (const auto precision = precisions.get(entity)) {
            next = std::get<1>(*precision).round(next);
        }
        next = range.clamp(next);

        if (mouse->just_released(MouseButton::Left)) {
            if (drag->changed || current_pointer != drag->pointer_start) {
                changed.send(
                    ValueChange<float> {
                        .source = entity,
                        .value = next,
                        .is_final = true,
                    }
                );
            }
            drag = SliderDragState {};
            commands.entity(entity).remove<ui::Pressed>();
        } else if (
            mouse->pressed(MouseButton::Left) &&
            current_pointer != drag->pointer_position
        ) {
            changed.send(
                ValueChange<float> {
                    .source = entity,
                    .value = next,
                    .is_final = false,
                }
            );
            auto state = drag.read();
            state.changed = true;
            state.pointer_position = current_pointer;
            drag = state;
        }
    }

    while (const auto event = set_values.next()) {
        request(event->entity, event->change);
    }
}

void slider_self_update(
    EventReader<ValueChange<float>> changes,
    Query<Entity, const Slider> sliders,
    Commands commands
) {
    while (const auto change = changes.next()) {
        if (sliders.get(change->source)) {
            commands.entity(change->source)
                .add(SliderValue {.value = change->value});
        }
    }
}

} // namespace ets::ui_widgets
