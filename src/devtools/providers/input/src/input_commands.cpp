#include "input_commands.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace fei::devtools::input {

namespace {

using Json = nlohmann::json;

Result<KeyCode, std::string> parse_key_code(std::string_view name) {
    for (auto key : c_key_codes) {
        if (key != KeyCode::Unknown && name == key_code_to_string(key)) {
            return key;
        }
    }
    return failure(std::string {"input.key key is not a supported KeyCode"});
}

Result<Json, std::string> parse_json_object(std::string_view body) {
    auto text = body.empty() ? std::string_view {"{}"} : body;
    auto json = Json::parse(text, nullptr, false);
    if (json.is_discarded()) {
        return failure(std::string {"Command body must be valid JSON"});
    }
    if (!json.is_object()) {
        return failure(std::string {"Command body must be a JSON object"});
    }
    return json;
}

} // namespace

Result<KeyCommandBody, std::string>
parse_key_command_body(std::string_view body) {
    auto json_result = parse_json_object(body);
    if (!json_result) {
        return failure(std::move(json_result.error()));
    }

    const auto& json = *json_result;
    if (json.size() != 2 || !json.contains("key") || !json.contains("down")) {
        return failure(
            std::string {
                R"(input.key body must be {"key":"<KeyCode>","down":<bool>})"
            }
        );
    }

    if (!json["key"].is_string()) {
        return failure(std::string {"input.key key must be a KeyCode string"});
    }
    auto key_result = parse_key_code(json["key"].get<std::string>());
    if (!key_result) {
        return failure(std::move(key_result.error()));
    }

    if (!json["down"].is_boolean()) {
        return failure(std::string {"input.key down must be a boolean"});
    }

    return KeyCommandBody {
        .key = *key_result,
        .down = json["down"].get<bool>(),
    };
}

Status<std::string> validate_clear_command_body(std::string_view body) {
    auto json_result = parse_json_object(body);
    if (!json_result) {
        return failure(std::move(json_result.error()));
    }
    if (!json_result->empty()) {
        return failure(std::string {"input.clear body must be empty or {}"});
    }
    return {};
}

std::string key_command_response_json(KeyCommandBody command) {
    return Json {
        {"ok", true},
        {"key", key_code_to_string(command.key)},
        {"down", command.down},
    }
        .dump();
}

std::string clear_command_response_json() {
    return Json {{"ok", true}}.dump();
}

} // namespace fei::devtools::input
