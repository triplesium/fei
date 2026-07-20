#pragma once

#include "refl/reflect.hpp"
#include "window/input.hpp"

namespace fei::devtools::input {

struct FEI_REFLECT KeyInputRequest {
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

struct FEI_REFLECT KeyInputResponse {
    bool ok {true};
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

struct FEI_REFLECT ClearInputResponse {
    bool ok {true};
};

} // namespace fei::devtools::input
