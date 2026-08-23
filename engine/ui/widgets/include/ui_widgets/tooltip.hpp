#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"
#include "ui_widgets/popover.hpp"

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct Tooltip {
    Entity content;
    float delay {0.5f};
    PopoverPlacement placement {
        .side = PopoverSide::Top,
        .align = PopoverAlign::Center,
        .gap = 6.0f,
    };
};

ETS_REFLECT(Component)
struct TooltipState {
    float hovered_time {0.0f};
    bool visible {false};

    bool operator==(const TooltipState&) const = default;
};

} // namespace ets::ui_widgets
