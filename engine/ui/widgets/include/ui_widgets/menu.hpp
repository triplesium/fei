#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT()
enum class MenuAction {
    Opened,
    Closed,
    Activated,
};

ETS_REFLECT()
struct MenuEvent {
    Entity menu;
    Entity source;
    MenuAction action;

    bool operator==(const MenuEvent&) const = default;
};

ETS_REFLECT(Component)
struct MenuButton {
    Entity popup;
};

ETS_REFLECT(Component)
struct MenuPopup {};

ETS_REFLECT(Component)
struct MenuItem {};

ETS_REFLECT(Component)
struct MenuOpen {};

} // namespace ets::ui_widgets
