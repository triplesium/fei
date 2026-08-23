#pragma once

#include "refl/reflect.hpp"

namespace ets::ui_widgets {

ETS_REFLECT()
enum class ControlOrientation {
    Horizontal,
    Vertical,
};

} // namespace ets::ui_widgets
