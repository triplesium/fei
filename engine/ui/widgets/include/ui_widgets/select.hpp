#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct Select {
    Entity popup;
    Entity list_box;
};

// An editable select trigger. The input text remains application-owned; the
// widget emits SelectionChange when an option is chosen.
ETS_REFLECT(Component)
struct ComboBox {
    Entity popup;
    Entity list_box;
};

ETS_REFLECT(Component)
struct Expanded {};

ETS_REFLECT()
struct SelectionChange {
    Entity source;
    Entity option;

    bool operator==(const SelectionChange&) const = default;
};

} // namespace ets::ui_widgets
