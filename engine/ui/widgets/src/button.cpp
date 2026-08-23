#include "ui_widgets/plugin.hpp"

namespace ets::ui_widgets {

void sync_buttons(
    Query<Entity, const Button>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Button>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Button>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Commands commands
) {
    for (const auto& [entity, button] : missing_nodes) {
        (void)button;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, button] : missing_interactions) {
        (void)button;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, button] : missing_policies) {
        (void)button;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
}

void update_buttons(
    Query<Entity, const Button, const ui::Interaction> buttons,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const ActivateOnPress> activate_on_press,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    Commands commands,
    EventWriter<Activate> activated
) {
    for (const auto& [entity, button, interaction] : buttons) {
        (void)button;
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
            if (activate_on_press.get(entity)) {
                activated.send(Activate {.entity = entity});
            }
        }

        if (mouse->just_released(MouseButton::Left) && is_pressed) {
            if (interaction == ui::Interaction::Hovered &&
                !activate_on_press.get(entity)) {
                activated.send(Activate {.entity = entity});
            }
            commands.entity(entity).remove<ui::Pressed>();
        }
    }

    if ((keyboard->just_pressed(KeyCode::Enter) ||
         keyboard->just_pressed(KeyCode::Space)) &&
        input_focus->get()) {
        const auto entity = *input_focus->get();
        if (buttons.get(entity) && !disabled.get(entity)) {
            activated.send(Activate {.entity = entity});
        }
    }
}

} // namespace ets::ui_widgets
