#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct ScrollArea {};

ETS_REFLECT()
struct ScrollIntoView {
    Entity entity;

    bool operator==(const ScrollIntoView&) const = default;
};

} // namespace ets::ui_widgets
