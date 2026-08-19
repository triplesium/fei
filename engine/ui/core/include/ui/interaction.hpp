#pragma once

#include "base/optional.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

namespace fei::ui {

FEI_REFLECT(Component)
enum class Interaction {
    Pressed,
    Hovered,
    None,
};

FEI_REFLECT(Component)
enum class FocusPolicy {
    Block,
    Pass,
};

FEI_REFLECT(Component)
struct RelativeCursorPosition {
    bool cursor_over {false};
    Optional<Vector2> normalized;

    [[nodiscard]] bool is_cursor_over() const { return cursor_over; }

    bool operator==(const RelativeCursorPosition&) const = default;
};

FEI_REFLECT(Component)
struct InteractionDisabled {};

FEI_REFLECT(Component)
struct Pressed {};

FEI_REFLECT(Component)
struct Checkable {};

FEI_REFLECT(Component)
struct Checked {};

FEI_REFLECT(Component)
struct Selectable {};

FEI_REFLECT(Component)
struct Selected {};

} // namespace fei::ui
