#include "ui_widgets/plugin.hpp"

namespace fei::ui_widgets {

namespace {

bool is_descendant_of(
    Entity entity,
    Entity ancestor,
    const Query<Entity, const ChildOf>& parents
) {
    auto current = entity;
    while (true) {
        if (current == ancestor) {
            return true;
        }
        const auto parent = parents.get(current);
        if (!parent) {
            return false;
        }
        current = std::get<1>(*parent).parent;
    }
}

} // namespace

void sync_selects(
    Query<Entity, const Select>::Filter<Without<Button>> missing_select_buttons,
    Query<Entity, const Select>::Filter<Without<input_focus::TabIndex>>
        missing_select_indices,
    Query<Entity, const ComboBox>::Filter<Without<TextInput>>
        missing_combo_inputs,
    Query<Entity, const ComboBox>::Filter<Without<input_focus::TabIndex>>
        missing_combo_indices,
    Query<Entity, const Select> selects,
    Query<Entity, const ComboBox> combo_boxes,
    Query<Entity, const Popover> popovers,
    Query<Entity, const ui::Node> nodes,
    Commands commands
) {
    const auto ensure_popup_node = [&](Entity popup) {
        if (nodes.get(popup)) {
            return;
        }
        commands.add_command([popup](World& world) {
            if (world.has_component<ui::Node>(popup)) {
                auto node = world.get_component_rw<ui::Node>(popup);
                auto next = node.read();
                next.display = ui::Display::None;
                next.position_type = ui::PositionType::Absolute;
                node = next;
            } else {
                world.add_component(
                    popup,
                    ui::Node {
                        .display = ui::Display::None,
                        .position_type = ui::PositionType::Absolute,
                    }
                );
            }
        });
    };
    for (const auto& [entity, select] : missing_select_buttons) {
        (void)select;
        commands.entity(entity).add(Button {});
    }
    for (const auto& [entity, select] : missing_select_indices) {
        (void)select;
        commands.entity(entity).add(input_focus::TabIndex {});
    }
    for (const auto& [entity, combo] : missing_combo_inputs) {
        (void)combo;
        commands.entity(entity).add(TextInput {});
    }
    for (const auto& [entity, combo] : missing_combo_indices) {
        (void)combo;
        commands.entity(entity).add(input_focus::TabIndex {});
    }
    for (const auto& [entity, select] : selects) {
        ensure_popup_node(select.popup);
        if (!popovers.get(select.popup)) {
            commands.entity(select.popup).add(Popover {.anchor = entity});
        }
    }
    for (const auto& [entity, combo] : combo_boxes) {
        ensure_popup_node(combo.popup);
        if (!popovers.get(combo.popup)) {
            commands.entity(combo.popup).add(Popover {.anchor = entity});
        }
    }
}

void update_selects(
    Query<Entity, const Select> selects,
    Query<Entity, const ComboBox, const ui::Interaction> combo_boxes,
    Query<Entity, ui::Node> nodes,
    Query<Entity, const ListBox> list_boxes,
    Query<Entity, const ListItem> items,
    Query<Entity, const ui::Selected> selected,
    Query<Entity, const ui::Interaction> interactions,
    Query<Entity, ActiveDescendant> active_descendants,
    Query<Entity, const ChildOf> parents,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRW<input_focus::InputFocus> input_focus,
    ResRW<input_focus::InputFocusVisible> input_focus_visible,
    EventReader<Activate> activated,
    EventReader<ValueChange<Entity>> list_changes,
    Commands commands,
    EventWriter<SelectionChange> selection_changes
) {
    const auto set_open = [&](Entity source,
                              Entity popup,
                              Entity list_box,
                              bool open) {
        const auto popup_node = nodes.get(popup);
        if (!popup_node) {
            return;
        }
        auto node = std::get<1>(*popup_node);
        auto next = node.read();
        next.display = open ? ui::Display::Flex : ui::Display::None;
        if (next.display != node.read().display) {
            node = next;
        }
        if (open) {
            commands.entity(source).add(Expanded {});
            if (const auto active = active_descendants.get(list_box)) {
                auto active_value = std::get<1>(*active);
                for (const auto& [item, list_item] : items) {
                    (void)list_item;
                    if (selected.get(item) &&
                        is_descendant_of(item, list_box, parents)) {
                        active_value = ActiveDescendant {.entity = item};
                        break;
                    }
                }
            }
            input_focus->set(list_box, input_focus::FocusCause::Programmatic);
            input_focus_visible->visible = true;
        } else {
            commands.add_command([source](World& world) {
                if (world.has_component<Expanded>(source)) {
                    world.remove_component<Expanded>(source);
                }
            });
            input_focus->set(source, input_focus::FocusCause::Programmatic);
        }
    };
    const auto is_open = [&](Entity popup) {
        const auto node = nodes.get(popup);
        return node && std::get<1>(*node).read().display != ui::Display::None;
    };

    while (const auto event = activated.next()) {
        if (const auto select = selects.get(event->entity)) {
            const auto& value = std::get<1>(*select);
            set_open(
                event->entity,
                value.popup,
                value.list_box,
                !is_open(value.popup)
            );
        }
    }
    if (mouse->just_pressed(MouseButton::Left)) {
        for (const auto& [entity, combo, interaction] : combo_boxes) {
            if (interaction == ui::Interaction::Pressed &&
                !is_open(combo.popup)) {
                set_open(entity, combo.popup, combo.list_box, true);
            }
        }
    }
    if (keyboard->just_pressed(KeyCode::Down) && input_focus->get()) {
        const auto focused_combo = combo_boxes.get(*input_focus->get());
        if (focused_combo) {
            const auto& combo = std::get<1>(*focused_combo);
            if (!is_open(combo.popup)) {
                set_open(
                    *input_focus->get(),
                    combo.popup,
                    combo.list_box,
                    true
                );
            }
        }
    }

    while (const auto change = list_changes.next()) {
        if (!list_boxes.get(change->source) || !items.get(change->value)) {
            continue;
        }
        for (const auto& [entity, select] : selects) {
            if (select.list_box == change->source) {
                selection_changes.send(
                    SelectionChange {.source = entity, .option = change->value}
                );
                set_open(entity, select.popup, select.list_box, false);
            }
        }
        for (const auto& [entity, combo, interaction] : combo_boxes) {
            (void)interaction;
            if (combo.list_box == change->source) {
                selection_changes.send(
                    SelectionChange {.source = entity, .option = change->value}
                );
                set_open(entity, combo.popup, combo.list_box, false);
            }
        }
    }

    const bool escape = keyboard->just_pressed(KeyCode::Escape);
    const bool clicked = mouse->just_pressed(MouseButton::Left);
    if (!escape && !clicked) {
        return;
    }
    const auto should_close = [&](Entity source, Entity popup) {
        if (!is_open(popup)) {
            return false;
        }
        if (escape) {
            return true;
        }
        for (const auto& [entity, interaction] : interactions) {
            if (interaction == ui::Interaction::Pressed &&
                (entity == source ||
                 is_descendant_of(entity, popup, parents))) {
                return false;
            }
        }
        return true;
    };
    for (const auto& [entity, select] : selects) {
        if (should_close(entity, select.popup)) {
            set_open(entity, select.popup, select.list_box, false);
        }
    }
    for (const auto& [entity, combo, interaction] : combo_boxes) {
        (void)interaction;
        if (should_close(entity, combo.popup)) {
            set_open(entity, combo.popup, combo.list_box, false);
        }
    }
}

} // namespace fei::ui_widgets
