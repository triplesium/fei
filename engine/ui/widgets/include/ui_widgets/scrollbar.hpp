#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"
#include "ui_widgets/control.hpp"

namespace ets::ui_widgets {

ETS_REFLECT(Component)
struct Scrollbar {
    Entity target;
    ControlOrientation orientation {ControlOrientation::Vertical};
    float min_thumb_length {8.0f};

    [[nodiscard]] static Scrollbar new_scrollbar(
        Entity target,
        ControlOrientation orientation,
        float min_thumb_length
    ) {
        return {
            .target = target,
            .orientation = orientation,
            .min_thumb_length = min_thumb_length,
        };
    }
};

ETS_REFLECT(Component)
struct ScrollbarThumb {};

ETS_REFLECT(Component)
struct ScrollbarDragState {
    bool dragging {false};
    float offset {0.0f};
    float pointer_start {0.0f};
    float pointer_position {0.0f};
};

} // namespace ets::ui_widgets
