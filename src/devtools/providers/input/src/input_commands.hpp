#pragma once

#include "base/result.hpp"
#include "refl/reflect.hpp"
#include "window/input.hpp"

#include <string>
#include <string_view>

namespace fei::devtools::input {

struct FEI_REFLECT KeyCommandBody {
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

struct FEI_REFLECT KeyCommandResponse {
    bool ok {true};
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

struct FEI_REFLECT ClearCommandBody {};

struct FEI_REFLECT ClearCommandResponse {
    bool ok {true};
};

Result<KeyCommandBody, std::string>
parse_key_command_body(std::string_view body);
Status<std::string> validate_clear_command_body(std::string_view body);
Result<std::string, std::string>
key_command_response_json(KeyCommandBody command);
Result<std::string, std::string> clear_command_response_json();

} // namespace fei::devtools::input
