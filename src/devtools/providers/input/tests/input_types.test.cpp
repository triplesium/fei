#include "input_types.hpp"

#include "devtools/json.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

using namespace fei;
using namespace fei::devtools;
using namespace fei::devtools::input;

namespace {

void register_input_test_types() {
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
    registry.register_cls<KeyInputRequest>()
        .add_property("key", &KeyInputRequest::key)
        .add_property("down", &KeyInputRequest::down);
    registry.register_cls<KeyInputResponse>()
        .add_property("ok", &KeyInputResponse::ok)
        .add_property("key", &KeyInputResponse::key)
        .add_property("down", &KeyInputResponse::down);
    registry.register_cls<ClearInputResponse>().add_property(
        "ok",
        &ClearInputResponse::ok
    );
    registered = true;
}

} // namespace

TEST_CASE("input.key accepts strict reflected requests", "[devtools][input]") {
    register_input_test_types();

    auto pressed = decode_json<KeyInputRequest>(R"({"key":"W","down":true})");
    REQUIRE(pressed);
    REQUIRE(pressed->key == KeyCode::W);
    REQUIRE(pressed->down);

    KeyInputResponse response_value {
        .ok = true,
        .key = pressed->key,
        .down = pressed->down,
    };
    auto response = encode_json(Ref(response_value));
    REQUIRE(response);
    REQUIRE(response->find(R"("ok":true)") != std::string::npos);
    REQUIRE(response->find(R"("key":"W")") != std::string::npos);
}

TEST_CASE("input.key rejects invalid requests", "[devtools][input]") {
    register_input_test_types();

    REQUIRE_FALSE(decode_json<KeyInputRequest>("").has_value());
    REQUIRE_FALSE(decode_json<KeyInputRequest>(R"({"key":"W"})").has_value());
    REQUIRE_FALSE(
        decode_json<KeyInputRequest>(R"({"key":87,"down":true})").has_value()
    );
    REQUIRE_FALSE(
        decode_json<KeyInputRequest>(R"({"key":"W","down":true,"repeat":true})")
            .has_value()
    );
}
