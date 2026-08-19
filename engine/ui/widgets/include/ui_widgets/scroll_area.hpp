#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace fei::ui_widgets {

FEI_REFLECT(Component)
struct ScrollArea {};

FEI_REFLECT()
struct ScrollIntoView {
    Entity entity;

    bool operator==(const ScrollIntoView&) const = default;
};

} // namespace fei::ui_widgets
