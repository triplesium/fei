#include "input_commands.hpp"

#include "devtools/json.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace fei::devtools::input {

Result<KeyCommandBody, std::string>
parse_key_command_body(std::string_view body) {
    auto command = decode_json<KeyCommandBody>(body);
    if (!command) {
        return failure(std::move(command.error()));
    }
    if (command->key == KeyCode::Unknown) {
        return failure(std::string {"input.key key is not supported"});
    }
    return *command;
}

Status<std::string> validate_clear_command_body(std::string_view body) {
    auto text = body.empty() ? std::string_view {"{}"} : body;
    auto command = decode_json<ClearCommandBody>(text);
    if (!command) {
        return failure(std::move(command.error()));
    }
    return {};
}

Result<std::string, std::string>
key_command_response_json(KeyCommandBody command) {
    KeyCommandResponse response {
        .ok = true,
        .key = command.key,
        .down = command.down,
    };
    return encode_json(Ref(response));
}

Result<std::string, std::string> clear_command_response_json() {
    ClearCommandResponse response;
    return encode_json(Ref(response));
}

} // namespace fei::devtools::input
