#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace fei::ui_widgets {

FEI_REFLECT(Component)
struct Button {};

FEI_REFLECT(Component)
struct ActivateOnPress {};

FEI_REFLECT()
struct Activate {
    Entity entity;

    bool operator==(const Activate&) const = default;
};

} // namespace fei::ui_widgets
