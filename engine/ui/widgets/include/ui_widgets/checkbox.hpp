#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct Checkbox {};

ETS_REFLECT()
struct SetChecked {
    Entity entity;
    bool checked;

    bool operator==(const SetChecked&) const = default;
};

ETS_REFLECT()
struct ToggleChecked {
    Entity entity;

    bool operator==(const ToggleChecked&) const = default;
};

} // namespace ets::ui_widgets
