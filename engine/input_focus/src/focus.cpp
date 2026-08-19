#include "input_focus/focus.hpp"

namespace fei::input_focus {

void clear_invalid_focus(Query<Entity> entities, ResRW<InputFocus> focus) {
    const auto current =
        static_cast<const ResRW<InputFocus>&>(focus).get().get();
    if (current && !entities.get(*current)) {
        focus->clear();
    }
}

void apply_auto_focus(
    Query<Entity, const AutoFocus>::Filter<Added<AutoFocus>> added,
    ResRW<InputFocus> focus
) {
    for (const auto& [entity, auto_focus] : added) {
        (void)auto_focus;
        focus->set(entity, FocusCause::Programmatic);
    }
}

void process_focus_changes(
    ResRW<InputFocus> focus,
    EventWriter<FocusGained> gained,
    EventWriter<FocusLost> lost
) {
    const auto& read = static_cast<const ResRW<InputFocus>&>(focus).get();
    const auto current = read.entity;
    const auto previous = read.notified_entity;
    const auto cause = read.cause;
    if (current == previous) {
        return;
    }

    if (previous) {
        lost.send(FocusLost {.entity = *previous});
    }
    if (current) {
        gained.send(FocusGained {.entity = *current, .cause = cause});
    }
    focus.get().notified_entity = current;
}

bool is_focused(const InputFocus& focus, Entity entity) {
    const auto focused = focus.get();
    return focused && *focused == entity;
}

bool is_focus_within(
    const InputFocus& focus,
    Entity entity,
    const Query<Entity, const ChildOf>& parents
) {
    const auto focused = focus.get();
    if (!focused) {
        return false;
    }
    auto current = *focused;
    while (true) {
        if (current == entity) {
            return true;
        }
        const auto parent = parents.get(current);
        if (!parent) {
            return false;
        }
        current = std::get<1>(*parent).parent;
    }
}

bool is_focus_visible(
    const InputFocus& focus,
    const InputFocusVisible& visible,
    Entity entity
) {
    return visible.visible && is_focused(focus, entity);
}

bool is_focus_within_visible(
    const InputFocus& focus,
    const InputFocusVisible& visible,
    Entity entity,
    const Query<Entity, const ChildOf>& parents
) {
    return visible.visible && is_focus_within(focus, entity, parents);
}

} // namespace fei::input_focus
