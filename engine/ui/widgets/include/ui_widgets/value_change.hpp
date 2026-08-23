#pragma once

#include "ecs/fwd.hpp"

namespace ets::ui_widgets {

template<typename T>
struct ValueChange {
    Entity source;
    T value;
    bool is_final {true};

    bool operator==(const ValueChange&) const = default;
};

} // namespace ets::ui_widgets
