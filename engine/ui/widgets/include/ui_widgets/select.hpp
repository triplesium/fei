#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace fei::ui_widgets {

FEI_REFLECT(Component)
struct Select {
    Entity popup;
    Entity list_box;
};

// An editable select trigger. The input text remains application-owned; the
// widget emits SelectionChange when an option is chosen.
FEI_REFLECT(Component)
struct ComboBox {
    Entity popup;
    Entity list_box;
};

FEI_REFLECT(Component)
struct Expanded {};

FEI_REFLECT()
struct SelectionChange {
    Entity source;
    Entity option;

    bool operator==(const SelectionChange&) const = default;
};

} // namespace fei::ui_widgets
