#include "input_commands.hpp"

#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

using namespace fei;
using namespace fei::devtools::input;

namespace {

void register_input_command_test_types() {
    static bool registered = false;
    if (registered) {
        return;
    }

    auto& registry = Registry::instance();
    auto& key_code = registry.register_enum<KeyCode>();
    for (auto key : c_key_codes) {
        key_code.add_enumerator(
            key_code_to_string(key),
            static_cast<std::int64_t>(key)
        );
    }
    registry.register_cls<KeyCommandBody>()
        .add_property("key", &KeyCommandBody::key)
        .add_property("down", &KeyCommandBody::down);
    registry.register_cls<KeyCommandResponse>()
        .add_property("ok", &KeyCommandResponse::ok)
        .add_property("key", &KeyCommandResponse::key)
        .add_property("down", &KeyCommandResponse::down);
    registry.register_cls<ClearCommandBody>();
    registry.register_cls<ClearCommandResponse>().add_property(
        "ok",
        &ClearCommandResponse::ok
    );
    registered = true;
}

} // namespace

TEST_CASE(
    "input.key v1 accepts key names and boolean down",
    "[devtools][input]"
) {
    register_input_command_test_types();

    auto pressed = parse_key_command_body(R"({"key":"W","down":true})");
    REQUIRE(pressed);
    REQUIRE(pressed->key == KeyCode::W);
    REQUIRE(pressed->down);

    auto released =
        parse_key_command_body(R"({"key":"LeftShift","down":false})");
    REQUIRE(released);
    REQUIRE(released->key == KeyCode::LeftShift);
    REQUIRE_FALSE(released->down);

    auto response = key_command_response_json(*pressed);
    REQUIRE(response);
    REQUIRE(response->find(R"("ok":true)") != std::string::npos);
    REQUIRE(response->find(R"("key":"W")") != std::string::npos);
    REQUIRE(response->find(R"("down":true)") != std::string::npos);
}

TEST_CASE(
    "input.key v1 rejects loose or ambiguous bodies",
    "[devtools][input]"
) {
    register_input_command_test_types();

    REQUIRE_FALSE(parse_key_command_body("").has_value());
    auto missing = parse_key_command_body(R"({"key":"W"})");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().find("$.down") != std::string::npos);

    auto numeric = parse_key_command_body(R"({"key":87,"down":true})");
    REQUIRE_FALSE(numeric);
    REQUIRE(numeric.error().find("$.key") != std::string::npos);
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":"W","down":1})").has_value()
    );
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":"Unknown","down":true})").has_value()
    );
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":"NoSuchKey","down":true})").has_value()
    );
    auto unknown =
        parse_key_command_body(R"({"key":"W","down":true,"repeat":true})");
    REQUIRE_FALSE(unknown);
    REQUIRE(unknown.error().find("$.repeat") != std::string::npos);
}

TEST_CASE(
    "input.clear v1 accepts only empty object bodies",
    "[devtools][input]"
) {
    register_input_command_test_types();

    REQUIRE(validate_clear_command_body(""));
    REQUIRE(validate_clear_command_body("{}"));
    auto response = clear_command_response_json();
    REQUIRE(response);
    REQUIRE(response->find(R"("ok":true)") != std::string::npos);

    REQUIRE_FALSE(validate_clear_command_body("[]").has_value());
    REQUIRE_FALSE(validate_clear_command_body(R"({"key":"W"})").has_value());
    REQUIRE_FALSE(validate_clear_command_body("{").has_value());
}
