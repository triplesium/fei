#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT()
enum class PopoverSide {
    Bottom,
    Top,
    Right,
    Left,
};

ETS_REFLECT()
enum class PopoverAlign {
    Start,
    Center,
    End,
};

ETS_REFLECT()
struct PopoverPlacement {
    PopoverSide side {PopoverSide::Bottom};
    PopoverAlign align {PopoverAlign::Start};
    float gap {4.0f};
    float viewport_margin {8.0f};
    bool flip {true};
};

// An unstyled popup positioned relative to an anchor entity. The popover must
// be an absolute child of a viewport-sized UI node.
ETS_REFLECT(Component)
struct Popover {
    Entity anchor;
    PopoverPlacement placement;
};

} // namespace ets::ui_widgets
