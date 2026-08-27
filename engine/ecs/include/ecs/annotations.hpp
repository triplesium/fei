#pragma once

#include "refl/reflect.hpp"

namespace ets::annotations {

ETS_ANNOTATION(Component)
struct Component {};

ETS_ANNOTATION(Resource)
struct Resource {
    bool main_thread_only {false};
};

} // namespace ets::annotations
