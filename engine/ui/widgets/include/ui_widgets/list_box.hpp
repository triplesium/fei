#pragma once

#include "base/optional.hpp"
#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace fei::ui_widgets {

FEI_REFLECT(Component)
struct ListBox {};

FEI_REFLECT(Component)
struct ListItem {};

// The keyboard-highlighted row. Selection remains external until the
// application runs list_box_self_update.
FEI_REFLECT(Component)
struct ActiveDescendant {
    Optional<Entity> entity;

    bool operator==(const ActiveDescendant&) const = default;
};

FEI_REFLECT()
struct SetListSelection {
    Entity list_box;
    Entity item;
};

} // namespace fei::ui_widgets
