#pragma once

#include "base/result.hpp"
#include "window/input.hpp"

#include <string>
#include <string_view>

namespace fei::devtools::input {

struct KeyCommandBody {
    KeyCode key {KeyCode::Unknown};
    bool down {false};
};

Result<KeyCommandBody, std::string>
parse_key_command_body(std::string_view body);
Status<std::string> validate_clear_command_body(std::string_view body);
std::string key_command_response_json(KeyCommandBody command);
std::string clear_command_response_json();

} // namespace fei::devtools::input
