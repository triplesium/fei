#include "ui_widgets/plugin.hpp"

namespace fei::ui_widgets {

void sync_checkboxes(
    Query<Entity, const Checkbox>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Checkbox>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Checkbox>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const Checkbox>::Filter<Without<ui::Checkable>>
        missing_checkable,
    Commands commands
) {
    for (const auto& [entity, checkbox] : missing_nodes) {
        (void)checkbox;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, checkbox] : missing_interactions) {
        (void)checkbox;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, checkbox] : missing_policies) {
        (void)checkbox;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
    for (const auto& [entity, checkbox] : missing_checkable) {
        (void)checkbox;
        commands.entity(entity).add(ui::Checkable {});
    }
}

void update_checkboxes(
    Query<Entity, const Checkbox, const ui::Interaction> checkboxes,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventReader<SetChecked> set_checked,
    EventReader<ToggleChecked> toggle_checked,
    Commands commands,
    EventWriter<ValueChange<bool>> changed
) {
    const auto request_value = [&](Entity entity, bool value) {
        if (!checkboxes.get(entity) || disabled.get(entity) ||
            checked.get(entity).has_value() == value) {
            return;
        }
        changed.send(
            ValueChange<bool> {
                .source = entity,
                .value = value,
                .is_final = true,
            }
        );
    };

    for (const auto& [entity, checkbox, interaction] : checkboxes) {
        (void)checkbox;
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
                request_value(entity, !checked.get(entity));
            }
            commands.entity(entity).remove<ui::Pressed>();
        }
    }

    if ((keyboard->just_pressed(KeyCode::Enter) ||
         keyboard->just_pressed(KeyCode::Space)) &&
        input_focus->get()) {
        const auto entity = *input_focus->get();
        request_value(entity, !checked.get(entity));
    }

    while (const auto event = set_checked.next()) {
        request_value(event->entity, event->checked);
    }
    while (const auto event = toggle_checked.next()) {
        request_value(event->entity, !checked.get(event->entity));
    }
}

void checkbox_self_update(
    EventReader<ValueChange<bool>> changes,
    Query<Entity, const Checkbox> checkboxes,
    Query<Entity, const ui::Checked> checked,
    Commands commands
) {
    while (const auto change = changes.next()) {
        if (!checkboxes.get(change->source)) {
            continue;
        }
        const bool is_checked = checked.get(change->source).has_value();
        if (change->value && !is_checked) {
            commands.entity(change->source).add(ui::Checked {});
        } else if (!change->value && is_checked) {
            commands.entity(change->source).remove<ui::Checked>();
        }
    }
}

} // namespace fei::ui_widgets
