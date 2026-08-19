#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

#include <cstddef>
#include <string>

namespace fei::ui_widgets {

FEI_REFLECT(Component)
struct TextInput {};

FEI_REFLECT(Component)
struct TextInputState {
    bool changed {false};
    bool pointer_dragging {false};
    bool pointer_moved {false};
    bool select_all_on_release {false};
};

// Unstyled child decoration positioned by TextInputPlugin.
FEI_REFLECT(Component)
struct TextCaret {};

// Add one child per visual line that may need a selection rectangle.
FEI_REFLECT(Component)
struct TextSelection {
    std::size_t segment {0};
};

FEI_REFLECT(Component)
struct SelectAllOnFocus {};

FEI_REFLECT()
struct TextSubmit {
    Entity entity;
    std::string value;

    bool operator==(const TextSubmit&) const = default;
};

} // namespace fei::ui_widgets
