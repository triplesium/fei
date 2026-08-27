#pragma once

#include "refl/reflect.hpp"

#include <string>

namespace ets::annotations {

ETS_ANNOTATION(ScriptPrelude)
struct ScriptPrelude {};

ETS_ANNOTATION(ScriptModule)
struct ScriptModule {
    std::string name;
};

} // namespace ets::annotations
