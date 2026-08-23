#include "ui_widgets/plugin.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace ets::ui_widgets {

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

Optional<Entity> popup_for_item(
    Entity item,
    const Query<Entity, const MenuPopup, ui::Node>& popups,
    const Query<Entity, const ChildOf>& parents
) {
    auto current = item;
    while (const auto parent = parents.get(current)) {
        current = std::get<1>(*parent).parent;
        if (popups.get(current)) {
            return current;
        }
    }
    return nullopt;
}

std::vector<Entity> popup_items(
    Entity popup,
    const Query<Entity, const MenuPopup, ui::Node>& popups,
    const Query<Entity, const MenuItem>& menu_items,
    const Query<Entity, const ui::InteractionDisabled>& disabled,
    const Query<Entity, const Children>& child_lists
) {
    std::vector<Entity> result;
    std::function<void(Entity)> gather = [&](Entity entity) {
        if (entity != popup && popups.get(entity)) {
            return;
        }
        if (entity != popup && menu_items.get(entity) &&
            !disabled.get(entity)) {
            result.push_back(entity);
        }
        if (const auto children = child_lists.get(entity)) {
            for (const auto child : std::get<1>(*children)) {
                gather(child);
            }
        }
    };
    gather(popup);
    return result;
}

} // namespace

void sync_menus(
    Query<Entity, const MenuButton>::Filter<Without<Button>> missing_buttons,
    Query<Entity, const MenuButton>::Filter<Without<input_focus::TabIndex>>
        missing_button_indices,
    Query<Entity, const MenuPopup>::Filter<Without<ui::Node>> missing_popups,
    Query<Entity, const MenuItem>::Filter<Without<Button>> missing_items,
    Query<Entity, const MenuItem>::Filter<Without<input_focus::TabIndex>>
        missing_item_indices,
    Query<Entity, const MenuButton> menu_buttons,
    Query<Entity, const Popover> popovers,
    Commands commands
) {
    for (const auto& [entity, menu_button] : missing_buttons) {
        (void)menu_button;
        commands.entity(entity).add(Button {});
    }
    for (const auto& [entity, menu_button] : missing_button_indices) {
        (void)menu_button;
        commands.entity(entity).add(input_focus::TabIndex {});
    }
    for (const auto& [entity, popup] : missing_popups) {
        (void)popup;
        commands.entity(entity).add(
            ui::Node {
                .display = ui::Display::None,
                .position_type = ui::PositionType::Absolute,
            }
        );
    }
    for (const auto& [entity, item] : missing_items) {
        (void)item;
        commands.entity(entity).add(Button {});
    }
    for (const auto& [entity, item] : missing_item_indices) {
        (void)item;
        commands.entity(entity).add(input_focus::TabIndex {.index = -1});
    }
    for (const auto& [entity, menu_button] : menu_buttons) {
        if (!popovers.get(menu_button.popup)) {
            commands.entity(menu_button.popup).add(Popover {.anchor = entity});
        }
    }
}

void update_menus(
    Query<Entity, const MenuButton> menu_buttons,
    Query<Entity, const MenuPopup, ui::Node> popups,
    Query<Entity, const MenuItem> menu_items,
    Query<Entity, const ui::Interaction> interactions,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRW<input_focus::InputFocus> input_focus,
    ResRW<input_focus::InputFocusVisible> input_focus_visible,
    EventReader<Activate> activated,
    Commands commands,
    EventWriter<MenuEvent> menu_events
) {
    const auto close = [&](Entity button, const MenuButton& menu_button) {
        const auto popup = popups.get(menu_button.popup);
        if (!popup || std::get<2>(*popup).read().display == ui::Display::None) {
            return;
        }
        auto node = std::get<2>(*popup);
        auto next = node.read();
        next.display = ui::Display::None;
        node = next;
        commands.add_command([button](World& world) {
            if (world.has_component<MenuOpen>(button)) {
                world.remove_component<MenuOpen>(button);
            }
        });
        input_focus->set(button, input_focus::FocusCause::Programmatic);
        menu_events.send(
            MenuEvent {
                .menu = menu_button.popup,
                .source = button,
                .action = MenuAction::Closed,
            }
        );
    };

    const auto open = [&](Entity button, const MenuButton& menu_button) {
        const auto popup = popups.get(menu_button.popup);
        if (!popup) {
            return;
        }
        auto node = std::get<2>(*popup);
        auto next = node.read();
        next.display = ui::Display::Flex;
        node = next;
        commands.entity(button).add(MenuOpen {});
        const auto items = popup_items(
            menu_button.popup,
            popups,
            menu_items,
            disabled,
            child_lists
        );
        if (!items.empty()) {
            input_focus->set(
                items.front(),
                input_focus::FocusCause::Programmatic
            );
        }
        input_focus_visible->visible = true;
        menu_events.send(
            MenuEvent {
                .menu = menu_button.popup,
                .source = button,
                .action = MenuAction::Opened,
            }
        );
    };

    while (const auto event = activated.next()) {
        if (const auto button = menu_buttons.get(event->entity)) {
            const auto& menu_button = std::get<1>(*button);
            const auto popup = popups.get(menu_button.popup);
            if (popup &&
                std::get<2>(*popup).read().display != ui::Display::None) {
                close(event->entity, menu_button);
            } else {
                for (const auto& [other, other_button] : menu_buttons) {
                    if (other != event->entity) {
                        close(other, other_button);
                    }
                }
                open(event->entity, menu_button);
            }
            continue;
        }
        if (!menu_items.get(event->entity) || disabled.get(event->entity)) {
            continue;
        }
        const auto popup = popup_for_item(event->entity, popups, parents);
        if (!popup) {
            continue;
        }
        menu_events.send(
            MenuEvent {
                .menu = *popup,
                .source = event->entity,
                .action = MenuAction::Activated,
            }
        );
        for (const auto& [button, menu_button] : menu_buttons) {
            if (menu_button.popup == *popup) {
                close(button, menu_button);
                break;
            }
        }
    }

    Optional<Entity> open_button;
    Optional<MenuButton> open_menu;
    for (const auto& [button, menu_button] : menu_buttons) {
        const auto popup = popups.get(menu_button.popup);
        if (popup && std::get<2>(*popup).read().display != ui::Display::None) {
            open_button = button;
            open_menu = menu_button;
            break;
        }
    }
    if (!open_button || !open_menu) {
        return;
    }

    if (keyboard->just_pressed(KeyCode::Escape)) {
        close(*open_button, *open_menu);
        return;
    }

    if (mouse->just_pressed(MouseButton::Left)) {
        bool inside = false;
        for (const auto& [entity, interaction] : interactions) {
            if (interaction == ui::Interaction::Pressed &&
                (is_descendant_of(entity, open_menu->popup, parents) ||
                 entity == *open_button)) {
                inside = true;
                break;
            }
        }
        if (!inside) {
            close(*open_button, *open_menu);
            return;
        }
    }

    const bool previous = keyboard->just_pressed(KeyCode::Up) ||
                          keyboard->just_pressed(KeyCode::Home);
    const bool next = keyboard->just_pressed(KeyCode::Down) ||
                      keyboard->just_pressed(KeyCode::End);
    if (!previous && !next) {
        return;
    }
    const auto items = popup_items(
        open_menu->popup,
        popups,
        menu_items,
        disabled,
        child_lists
    );
    if (items.empty()) {
        return;
    }
    const auto focused = input_focus->get();
    const auto current =
        std::find_if(items.begin(), items.end(), [&](Entity entity) {
            return focused == Optional<Entity> {entity};
        });
    std::size_t index = 0;
    if (keyboard->just_pressed(KeyCode::Home)) {
        index = 0;
    } else if (keyboard->just_pressed(KeyCode::End)) {
        index = items.size() - 1;
    } else if (current == items.end()) {
        index = previous ? items.size() - 1 : 0;
    } else {
        const auto current_index =
            static_cast<std::size_t>(std::distance(items.begin(), current));
        index = previous ? (current_index + items.size() - 1) % items.size() :
                           (current_index + 1) % items.size();
    }
    input_focus->set(items[index], input_focus::FocusCause::Navigation);
    input_focus_visible->visible = true;
}

} // namespace ets::ui_widgets
