#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace fei::ui_widgets {

FEI_REFLECT()
enum class MenuAction {
    Opened,
    Closed,
    Activated,
};

FEI_REFLECT()
struct MenuEvent {
    Entity menu;
    Entity source;
    MenuAction action;

    bool operator==(const MenuEvent&) const = default;
};

FEI_REFLECT(Component)
struct MenuButton {
    Entity popup;
};

FEI_REFLECT(Component)
struct MenuPopup {};

FEI_REFLECT(Component)
struct MenuItem {};

FEI_REFLECT(Component)
struct MenuOpen {};

} // namespace fei::ui_widgets
