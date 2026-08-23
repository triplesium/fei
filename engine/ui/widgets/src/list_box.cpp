#include "ui_widgets/plugin.hpp"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ets::ui_widgets {

namespace {

template<typename Boxes>
Optional<Entity> box_for_item(
    Entity item,
    const Boxes& boxes,
    const Query<Entity, const ChildOf>& parents
) {
    auto current = item;
    while (const auto parent = parents.get(current)) {
        current = std::get<1>(*parent).parent;
        if (boxes.get(current)) {
            return current;
        }
    }
    return nullopt;
}

template<typename Boxes>
std::vector<Entity> box_items(
    Entity box,
    const Boxes& boxes,
    const Query<Entity, const ListItem>& items,
    const Query<Entity, const ui::InteractionDisabled>& disabled,
    const Query<Entity, const Children>& child_lists
) {
    std::vector<Entity> result;
    std::function<void(Entity)> gather = [&](Entity entity) {
        if (entity != box && boxes.get(entity)) {
            return;
        }
        if (entity != box && items.get(entity) && !disabled.get(entity)) {
            result.push_back(entity);
        }
        if (const auto children = child_lists.get(entity)) {
            for (const auto child : std::get<1>(*children)) {
                gather(child);
            }
        }
    };
    gather(box);
    return result;
}

} // namespace

void sync_list_boxes(
    Query<Entity, const ListBox>::Filter<Without<ui::Node>> missing_box_nodes,
    Query<Entity, const ListBox>::Filter<Without<ui::Interaction>>
        missing_box_interactions,
    Query<Entity, const ListBox>::Filter<Without<ui::FocusPolicy>>
        missing_box_policies,
    Query<Entity, const ListBox>::Filter<Without<input_focus::TabIndex>>
        missing_box_indices,
    Query<Entity, const ListBox>::Filter<Without<ActiveDescendant>>
        missing_active_descendants,
    Query<Entity, const ListItem>::Filter<Without<Button>> missing_item_buttons,
    Query<Entity, const ListItem>::Filter<Without<input_focus::TabIndex>>
        missing_item_indices,
    Query<Entity, const ListItem>::Filter<Without<ui::Selectable>>
        missing_selectable,
    Commands commands
) {
    for (const auto& [entity, box] : missing_box_nodes) {
        (void)box;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, box] : missing_box_interactions) {
        (void)box;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, box] : missing_box_policies) {
        (void)box;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
    for (const auto& [entity, box] : missing_box_indices) {
        (void)box;
        commands.entity(entity).add(input_focus::TabIndex {});
    }
    for (const auto& [entity, box] : missing_active_descendants) {
        (void)box;
        commands.entity(entity).add(ActiveDescendant {});
    }
    for (const auto& [entity, item] : missing_item_buttons) {
        (void)item;
        commands.entity(entity).add(Button {});
    }
    for (const auto& [entity, item] : missing_item_indices) {
        (void)item;
        commands.entity(entity).add(input_focus::TabIndex {.index = -1});
    }
    for (const auto& [entity, item] : missing_selectable) {
        (void)item;
        commands.entity(entity).add(ui::Selectable {});
    }
}

void update_list_boxes(
    Query<Entity, const ListBox, ActiveDescendant> list_boxes,
    Query<Entity, const ListItem> items,
    Query<Entity, const ui::Interaction> interactions,
    Query<Entity, const ui::Selected> selected,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventReader<Activate> activated,
    EventReader<SetListSelection> set_selection,
    EventWriter<ValueChange<Entity>> changed
) {
    const auto request = [&](Entity box, Entity item) {
        if (!list_boxes.get(box) || !items.get(item) || disabled.get(item) ||
            box_for_item(item, list_boxes, parents) != Optional<Entity> {box}) {
            return;
        }
        changed.send(
            ValueChange<Entity> {
                .source = box,
                .value = item,
                .is_final = true,
            }
        );
    };

    while (const auto event = activated.next()) {
        if (items.get(event->entity)) {
            if (const auto box =
                    box_for_item(event->entity, list_boxes, parents)) {
                request(*box, event->entity);
            }
        }
    }
    while (const auto event = set_selection.next()) {
        request(event->list_box, event->item);
    }

    for (auto [box, list_box, active] : list_boxes) {
        (void)list_box;
        auto available =
            box_items(box, list_boxes, items, disabled, child_lists);
        if (available.empty()) {
            if (active.read().entity) {
                active = ActiveDescendant {};
            }
            continue;
        }

        for (const auto item : available) {
            const auto interaction = interactions.get(item);
            if (interaction &&
                std::get<1>(*interaction) == ui::Interaction::Hovered &&
                active.read().entity != Optional<Entity> {item}) {
                active = ActiveDescendant {.entity = item};
                break;
            }
        }

        const auto focused = input_focus->get();
        const bool owns_focus =
            focused &&
            (*focused == box || box_for_item(*focused, list_boxes, parents) ==
                                    Optional<Entity> {box});
        if (!owns_focus) {
            continue;
        }
        if ((keyboard->just_pressed(KeyCode::Enter) ||
             keyboard->just_pressed(KeyCode::Space)) &&
            active.read().entity) {
            request(box, *active.read().entity);
        }
        const bool previous = keyboard->just_pressed(KeyCode::Up);
        const bool next = keyboard->just_pressed(KeyCode::Down);
        const bool first = keyboard->just_pressed(KeyCode::Home);
        const bool last = keyboard->just_pressed(KeyCode::End);
        if (!previous && !next && !first && !last) {
            continue;
        }
        auto current = std::find_if(
            available.begin(),
            available.end(),
            [&](Entity entity) {
                return active.read().entity == Optional<Entity> {entity};
            }
        );
        std::size_t index = 0;
        if (first) {
            index = 0;
        } else if (last) {
            index = available.size() - 1;
        } else if (current == available.end()) {
            const auto chosen = std::find_if(
                available.begin(),
                available.end(),
                [&](Entity entity) {
                    return selected.get(entity).has_value();
                }
            );
            if (chosen != available.end()) {
                current = chosen;
            }
            if (current == available.end()) {
                index = previous ? available.size() - 1 : 0;
            } else {
                const auto current_index = static_cast<std::size_t>(
                    std::distance(available.begin(), current)
                );
                index = previous ? (current_index + available.size() - 1) %
                                       available.size() :
                                   (current_index + 1) % available.size();
            }
        } else {
            const auto current_index = static_cast<std::size_t>(
                std::distance(available.begin(), current)
            );
            index = previous ? (current_index + available.size() - 1) %
                                   available.size() :
                               (current_index + 1) % available.size();
        }
        active = ActiveDescendant {.entity = available[index]};
    }
}

void list_box_self_update(
    EventReader<ValueChange<Entity>> changes,
    Query<Entity, const ListBox> list_boxes,
    Query<Entity, const ListItem> items,
    Query<Entity, const ui::Selected> selected,
    Query<Entity, const ChildOf> parents,
    Commands commands
) {
    std::unordered_map<Entity, Entity> selections;
    while (const auto change = changes.next()) {
        if (!list_boxes.get(change->source) || !items.get(change->value) ||
            box_for_item(change->value, list_boxes, parents) !=
                Optional<Entity> {change->source}) {
            continue;
        }
        selections.insert_or_assign(change->source, change->value);
    }
    for (const auto [list_box, selected_item] : selections) {
        for (const auto& [entity, item] : items) {
            (void)item;
            if (box_for_item(entity, list_boxes, parents) !=
                Optional<Entity> {list_box}) {
                continue;
            }
            if (entity == selected_item && !selected.get(entity)) {
                commands.entity(entity).add(ui::Selected {});
            } else if (entity != selected_item && selected.get(entity)) {
                commands.entity(entity).remove<ui::Selected>();
            }
        }
    }
}

} // namespace ets::ui_widgets
