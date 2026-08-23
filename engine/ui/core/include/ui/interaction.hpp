#pragma once

#include "base/optional.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

namespace ets::ui {

ETS_REFLECT(Component)
enum class Interaction {
    Pressed,
    Hovered,
    None,
};

ETS_REFLECT(Component)
enum class FocusPolicy {
    Block,
    Pass,
};

ETS_REFLECT(Component)
struct RelativeCursorPosition {
    bool cursor_over {false};
    Optional<Vector2> normalized;

    [[nodiscard]] bool is_cursor_over() const { return cursor_over; }

    bool operator==(const RelativeCursorPosition&) const = default;
};

ETS_REFLECT(Component)
struct InteractionDisabled {};

ETS_REFLECT(Component)
struct Pressed {};

ETS_REFLECT(Component)
struct Checkable {};

ETS_REFLECT(Component)
struct Checked {};

ETS_REFLECT(Component)
struct Selectable {};

ETS_REFLECT(Component)
struct Selected {};

} // namespace ets::ui
