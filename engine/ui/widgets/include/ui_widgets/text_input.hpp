#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

#include <cstddef>
#include <string>

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct TextInput {};

ETS_REFLECT(Component)
struct TextInputState {
    bool changed {false};
    bool pointer_dragging {false};
    bool pointer_moved {false};
    bool select_all_on_release {false};
};

// Unstyled child decoration positioned by TextInputPlugin.
ETS_REFLECT(Component)
struct TextCaret {};

// Add one child per visual line that may need a selection rectangle.
ETS_REFLECT(Component)
struct TextSelection {
    std::size_t segment {0};
};

ETS_REFLECT(Component)
struct SelectAllOnFocus {};

ETS_REFLECT()
struct TextSubmit {
    Entity entity;
    std::string value;

    bool operator==(const TextSubmit&) const = default;
};

} // namespace ets::ui_widgets
