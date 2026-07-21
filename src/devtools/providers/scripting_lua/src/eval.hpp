#pragma once

#include "devtools/types.hpp"
#include "ecs/fwd.hpp"
#include "eval_types.hpp"

#include <memory>
#include <string>

namespace fei {

class System;

namespace devtools::scripting_lua {

std::unique_ptr<System> make_eval_system(
    Entity request_entity,
    Token token,
    std::string capability,
    EvalRequest request,
    EvalLimits limits
);

} // namespace devtools::scripting_lua
} // namespace fei
