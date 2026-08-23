#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct Button {};

ETS_REFLECT(Component)
struct ActivateOnPress {};

ETS_REFLECT()
struct Activate {
    Entity entity;

    bool operator==(const Activate&) const = default;
};

} // namespace ets::ui_widgets
