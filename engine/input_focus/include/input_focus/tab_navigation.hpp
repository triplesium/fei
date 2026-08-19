#pragma once

#include "ecs/hierarchy.hpp"
#include "ecs/query.hpp"
#include "input_focus/focus.hpp"
#include "refl/reflect.hpp"
#include "window/input.hpp"

#include <cstdint>

namespace fei::input_focus {

FEI_REFLECT(Component)
struct TabIndex {
    std::int32_t index {0};

    bool operator==(const TabIndex&) const = default;
};

FEI_REFLECT(Component)
struct TabGroup {
    std::int32_t order {0};
    bool modal {false};

    [[nodiscard]] static TabGroup ordered(std::int32_t value) {
        return {.order = value};
    }

    [[nodiscard]] static TabGroup modal_group() { return {.modal = true}; }
};

FEI_REFLECT()
enum class NavAction {
    Next,
    Previous,
    First,
    Last,
};

void navigate_focus(
    Query<Entity, const TabGroup> groups,
    Query<Entity, const TabIndex> tab_indices,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    ResRO<KeyInput> keyboard,
    ResRW<InputFocus> focus,
    ResRW<InputFocusVisible> focus_visible
);

} // namespace fei::input_focus
