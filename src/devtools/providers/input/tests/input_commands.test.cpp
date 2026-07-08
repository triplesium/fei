#include "input_commands.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::devtools::input;

TEST_CASE(
    "input.key v1 accepts key names and boolean down",
    "[devtools][input]"
) {
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
    REQUIRE(response.find(R"("ok":true)") != std::string::npos);
    REQUIRE(response.find(R"("key":"W")") != std::string::npos);
    REQUIRE(response.find(R"("down":true)") != std::string::npos);
}

TEST_CASE(
    "input.key v1 rejects loose or ambiguous bodies",
    "[devtools][input]"
) {
    REQUIRE_FALSE(parse_key_command_body("").has_value());
    REQUIRE_FALSE(parse_key_command_body(R"({"key":"W"})").has_value());
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":87,"down":true})").has_value()
    );
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":"W","down":1})").has_value()
    );
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":"Unknown","down":true})").has_value()
    );
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":"NoSuchKey","down":true})").has_value()
    );
    REQUIRE_FALSE(
        parse_key_command_body(R"({"key":"W","down":true,"repeat":true})")
            .has_value()
    );
}

TEST_CASE(
    "input.clear v1 accepts only empty object bodies",
    "[devtools][input]"
) {
    REQUIRE(validate_clear_command_body(""));
    REQUIRE(validate_clear_command_body("{}"));
    REQUIRE(
        clear_command_response_json().find(R"("ok":true)") != std::string::npos
    );

    REQUIRE_FALSE(validate_clear_command_body("[]").has_value());
    REQUIRE_FALSE(validate_clear_command_body(R"({"key":"W"})").has_value());
    REQUIRE_FALSE(validate_clear_command_body("{").has_value());
}
