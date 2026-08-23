#include "ui_widgets/plugin.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ets::ui_widgets {

namespace {

Optional<Entity> group_for(
    Entity entity,
    const Query<Entity, const RadioGroup>& groups,
    const Query<Entity, const ChildOf>& parents
) {
    auto current = entity;
    while (const auto parent = parents.get(current)) {
        current = std::get<1>(*parent).parent;
        if (groups.get(current)) {
            return current;
        }
    }
    return nullopt;
}

std::vector<Entity> descendants(
    Entity root,
    const Query<Entity, const RadioButton>& radio_buttons,
    const Query<Entity, const RadioGroup>& groups,
    const Query<Entity, const Children>& child_lists
) {
    std::vector<Entity> result;
    std::function<void(Entity)> gather = [&](Entity entity) {
        if (entity != root && groups.get(entity)) {
            return;
        }
        if (entity != root && radio_buttons.get(entity)) {
            result.push_back(entity);
        }
        if (const auto children = child_lists.get(entity)) {
            for (const auto child : std::get<1>(*children)) {
                gather(child);
            }
        }
    };
    gather(root);
    return result;
}

} // namespace

void sync_radio_buttons(
    Query<Entity, const RadioButton>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const RadioButton>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const RadioButton>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const RadioButton>::Filter<Without<ui::Checkable>>
        missing_checkable,
    Commands commands
) {
    for (const auto& [entity, radio] : missing_nodes) {
        (void)radio;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, radio] : missing_interactions) {
        (void)radio;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, radio] : missing_policies) {
        (void)radio;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
    for (const auto& [entity, radio] : missing_checkable) {
        (void)radio;
        commands.entity(entity).add(ui::Checkable {});
    }
}

void update_radio_buttons(
    Query<Entity, const RadioButton, const ui::Interaction> radio_buttons,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    Commands commands,
    EventWriter<ValueChange<bool>> changed
) {
    const auto request_checked = [&](Entity entity) {
        if (!radio_buttons.get(entity) || disabled.get(entity) ||
            checked.get(entity)) {
            return;
        }
        changed.send(
            ValueChange<bool> {
                .source = entity,
                .value = true,
                .is_final = true,
            }
        );
    };

    for (const auto& [entity, radio, interaction] : radio_buttons) {
        (void)radio;
        const bool is_pressed = pressed.get(entity).has_value();
        const bool is_disabled = disabled.get(entity).has_value();

        if (is_disabled) {
            if (is_pressed) {
                commands.entity(entity).remove<ui::Pressed>();
            }
            continue;
        }

        if (mouse->just_pressed(MouseButton::Left) &&
            interaction == ui::Interaction::Pressed && !is_pressed) {
            commands.entity(entity).add(ui::Pressed {});
        }

        if (mouse->just_released(MouseButton::Left) && is_pressed) {
            if (interaction == ui::Interaction::Hovered) {
                request_checked(entity);
            }
            commands.entity(entity).remove<ui::Pressed>();
        }
    }

    if ((keyboard->just_pressed(KeyCode::Enter) ||
         keyboard->just_pressed(KeyCode::Space)) &&
        input_focus->get()) {
        request_checked(*input_focus->get());
    }
}

void navigate_radio_groups(
    Query<Entity, const RadioGroup> groups,
    Query<Entity, const RadioButton> radio_buttons,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventWriter<ValueChange<bool>> changed
) {
    const bool previous = keyboard->just_pressed(KeyCode::Left) ||
                          keyboard->just_pressed(KeyCode::Up);
    const bool next = keyboard->just_pressed(KeyCode::Right) ||
                      keyboard->just_pressed(KeyCode::Down);
    if ((!previous && !next) || !input_focus->get()) {
        return;
    }

    const auto group = *input_focus->get();
    if (!groups.get(group)) {
        return;
    }

    auto buttons = descendants(group, radio_buttons, groups, child_lists);
    std::erase_if(buttons, [&](Entity entity) {
        return disabled.get(entity).has_value();
    });
    if (buttons.empty()) {
        return;
    }

    const auto current =
        std::find_if(buttons.begin(), buttons.end(), [&](Entity entity) {
            return checked.get(entity).has_value();
        });
    const auto count = buttons.size();
    std::size_t selected = 0;
    if (current == buttons.end()) {
        selected = previous ? count - 1 : 0;
    } else {
        const auto index =
            static_cast<std::size_t>(std::distance(buttons.begin(), current));
        selected = previous ? (index + count - 1) % count : (index + 1) % count;
    }

    const auto entity = buttons[selected];
    if (!checked.get(entity)) {
        changed.send(
            ValueChange<bool> {
                .source = entity,
                .value = true,
                .is_final = true,
            }
        );
    }
}

void propagate_radio_changes(
    EventReader<ValueChange<bool>> changes,
    Query<Entity, const RadioButton> radio_buttons,
    Query<Entity, const RadioGroup> groups,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const ChildOf> parents,
    EventWriter<ValueChange<Entity>> selected
) {
    while (const auto change = changes.next()) {
        if (!change->value || !radio_buttons.get(change->source) ||
            disabled.get(change->source)) {
            continue;
        }
        if (const auto group = group_for(change->source, groups, parents)) {
            selected.send(
                ValueChange<Entity> {
                    .source = *group,
                    .value = change->source,
                    .is_final = change->is_final,
                }
            );
        }
    }
}

void radio_self_update(
    EventReader<ValueChange<Entity>> changes,
    Query<Entity, const RadioGroup> groups,
    Query<Entity, const RadioButton> radio_buttons,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    Commands commands
) {
    std::unordered_map<Entity, Entity> selections;
    while (const auto change = changes.next()) {
        if (!groups.get(change->source) || !radio_buttons.get(change->value) ||
            disabled.get(change->value) ||
            group_for(change->value, groups, parents) !=
                Optional<Entity> {change->source}) {
            continue;
        }
        selections.insert_or_assign(change->source, change->value);
    }

    for (const auto [group, selected] : selections) {
        for (const auto entity :
             descendants(group, radio_buttons, groups, child_lists)) {
            const bool is_checked = checked.get(entity).has_value();
            if (entity == selected && !is_checked) {
                commands.entity(entity).add(ui::Checked {});
            } else if (entity != selected && is_checked) {
                commands.entity(entity).remove<ui::Checked>();
            }
        }
    }
}

} // namespace ets::ui_widgets
