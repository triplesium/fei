#pragma once

#include "input/input.hpp"
#include "refl/reflect.hpp"

namespace ets::devtools::input {

ETS_REFLECT()
struct KeyInputRequest {
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

ETS_REFLECT()
struct KeyInputResponse {
    bool ok {true};
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

ETS_REFLECT()
struct ClearInputResponse {
    bool ok {true};
};

} // namespace ets::devtools::input
