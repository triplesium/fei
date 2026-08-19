#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace fei::ui_widgets {

FEI_REFLECT()
enum class PopoverSide {
    Bottom,
    Top,
    Right,
    Left,
};

FEI_REFLECT()
enum class PopoverAlign {
    Start,
    Center,
    End,
};

FEI_REFLECT()
struct PopoverPlacement {
    PopoverSide side {PopoverSide::Bottom};
    PopoverAlign align {PopoverAlign::Start};
    float gap {4.0f};
    float viewport_margin {8.0f};
    bool flip {true};
};

// An unstyled popup positioned relative to an anchor entity. The popover must
// be an absolute child of a viewport-sized UI node.
FEI_REFLECT(Component)
struct Popover {
    Entity anchor;
    PopoverPlacement placement;
};

} // namespace fei::ui_widgets
