#pragma once

#include "base/optional.hpp"
#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct ListBox {};

ETS_REFLECT(Component)
struct ListItem {};

// The keyboard-highlighted row. Selection remains external until the
// application runs list_box_self_update.
ETS_REFLECT(Component)
struct ActiveDescendant {
    Optional<Entity> entity;

    bool operator==(const ActiveDescendant&) const = default;
};

ETS_REFLECT()
struct SetListSelection {
    Entity list_box;
    Entity item;
};

} // namespace ets::ui_widgets
