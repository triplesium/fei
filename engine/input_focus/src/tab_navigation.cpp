#include "input_focus/tab_navigation.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <tuple>
#include <vector>

namespace ets::input_focus {

namespace {

struct Focusable {
    Entity entity;
    std::int32_t tab_index;
    std::size_t group_index;
};

Optional<Entity> modal_group_for(
    Entity entity,
    const Query<Entity, const TabGroup>& groups,
    const Query<Entity, const ChildOf>& parents
) {
    auto current = entity;
    while (true) {
        if (const auto group = groups.get(current);
            group && std::get<1>(*group).modal) {
            return current;
        }
        const auto parent = parents.get(current);
        if (!parent) {
            return nullopt;
        }
        current = std::get<1>(*parent).parent;
    }
}

} // namespace

void navigate_focus(
    Query<Entity, const TabGroup> groups,
    Query<Entity, const TabIndex> tab_indices,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    ResRO<KeyInput> keyboard,
    ResRW<InputFocus> focus,
    ResRW<InputFocusVisible> focus_visible
) {
    if (!keyboard->just_pressed(KeyCode::Tab)) {
        return;
    }

    const auto action = keyboard->pressed(KeyCode::LeftShift) ||
                                keyboard->pressed(KeyCode::RightShift) ?
                            NavAction::Previous :
                            NavAction::Next;
    const auto current_focus = focus->get();
    const auto modal_group =
        current_focus ? modal_group_for(*current_focus, groups, parents) :
                        Optional<Entity> {};

    std::vector<std::pair<Entity, TabGroup>> ordered_groups;
    for (const auto& [entity, group] : groups) {
        if (modal_group ? entity == *modal_group : !group.modal) {
            ordered_groups.emplace_back(entity, group);
        }
    }
    std::stable_sort(
        ordered_groups.begin(),
        ordered_groups.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.second.order < rhs.second.order;
        }
    );

    std::vector<Focusable> focusable;
    std::function<void(Entity, std::size_t)> gather =
        [&](Entity entity, std::size_t group_index) {
            if (groups.get(entity)) {
                return;
            }
            if (const auto item = tab_indices.get(entity)) {
                const auto index = std::get<1>(*item).index;
                if (index >= 0) {
                    focusable.push_back({entity, index, group_index});
                }
            }
            if (const auto children = child_lists.get(entity)) {
                for (const auto child : std::get<1>(*children)) {
                    gather(child, group_index);
                }
            }
        };

    for (std::size_t index = 0; index < ordered_groups.size(); ++index) {
        const auto children = child_lists.get(ordered_groups[index].first);
        if (!children) {
            continue;
        }
        for (const auto child : std::get<1>(*children)) {
            gather(child, index);
        }
    }
    if (focusable.empty()) {
        return;
    }

    std::stable_sort(
        focusable.begin(),
        focusable.end(),
        [](const Focusable& lhs, const Focusable& rhs) {
            return std::tie(lhs.group_index, lhs.tab_index) <
                   std::tie(rhs.group_index, rhs.tab_index);
        }
    );
    const auto current = std::find_if(
        focusable.begin(),
        focusable.end(),
        [&](const Focusable& item) {
            return current_focus && item.entity == *current_focus;
        }
    );
    const auto count = focusable.size();
    std::size_t next = 0;
    if (current == focusable.end()) {
        next = action == NavAction::Previous ? count - 1 : 0;
    } else {
        const auto index =
            static_cast<std::size_t>(std::distance(focusable.begin(), current));
        next = action == NavAction::Previous ? (index + count - 1) % count :
                                               (index + 1) % count;
    }

    if (focus->get() != Optional<Entity> {focusable[next].entity}) {
        focus->set(focusable[next].entity, FocusCause::Navigation);
    }
    if (!focus_visible->visible) {
        focus_visible->visible = true;
    }
}

} // namespace ets::input_focus
