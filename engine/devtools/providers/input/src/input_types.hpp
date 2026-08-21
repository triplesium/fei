#pragma once

#include "input/input.hpp"
#include "refl/reflect.hpp"

namespace fei::devtools::input {

FEI_REFLECT()
struct KeyInputRequest {
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

FEI_REFLECT()
struct KeyInputResponse {
    bool ok {true};
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

FEI_REFLECT()
struct ClearInputResponse {
    bool ok {true};
};

} // namespace fei::devtools::input
