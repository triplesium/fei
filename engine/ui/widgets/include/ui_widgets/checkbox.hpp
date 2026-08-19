#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace fei::ui_widgets {

FEI_REFLECT(Component)
struct Checkbox {};

FEI_REFLECT()
struct SetChecked {
    Entity entity;
    bool checked;

    bool operator==(const SetChecked&) const = default;
};

FEI_REFLECT()
struct ToggleChecked {
    Entity entity;

    bool operator==(const ToggleChecked&) const = default;
};

} // namespace fei::ui_widgets
