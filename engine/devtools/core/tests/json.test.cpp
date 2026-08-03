#include "devtools/json.hpp"

#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

using namespace fei;
using namespace fei::devtools;

namespace {

enum class JsonTestMode {
    Idle = 0,
    Active = 1,
};

struct JsonTestNested {
    bool enabled {false};
};

struct JsonTestPayload {
    int count {0};
    JsonTestMode mode {JsonTestMode::Idle};
    JsonTestNested nested;
};

void register_json_test_types() {
    static bool registered = false;
    if (registered) {
        return;
    }

    auto& registry = Registry::instance();
    registry.register_enum<JsonTestMode>()
        .add_enumerator("Idle", static_cast<std::int64_t>(JsonTestMode::Idle))
        .add_enumerator(
            "Active",
            static_cast<std::int64_t>(JsonTestMode::Active)
        );
    registry.register_cls<JsonTestNested>().add_property(
        "enabled",
        &JsonTestNested::enabled
    );
    registry.register_cls<JsonTestPayload>()
        .add_property("count", &JsonTestPayload::count)
        .add_property("mode", &JsonTestPayload::mode)
        .add_property("nested", &JsonTestPayload::nested);
    registered = true;
}

} // namespace

TEST_CASE(
    "DevTools JSON adapter round trips strict wire objects",
    "[devtools][json]"
) {
    register_json_test_types();

    JsonTestPayload expected {
        .count = 7,
        .mode = JsonTestMode::Active,
        .nested = JsonTestNested {.enabled = true},
    };
    auto text = encode_json(Ref(expected));
    REQUIRE(text);
    REQUIRE(
        *text == R"({"count":7,"mode":"Active","nested":{"enabled":true}})"
    );
    REQUIRE(text->find("$type") == std::string::npos);

    auto value = decode_json<JsonTestPayload>(*text);
    REQUIRE(value);
    const auto& actual = *value;
    REQUIRE(actual.count == expected.count);
    REQUIRE(actual.mode == expected.mode);
    REQUIRE(actual.nested.enabled == expected.nested.enabled);
}

TEST_CASE(
    "DevTools JSON adapter reports parse and serialization errors",
    "[devtools][json]"
) {
    register_json_test_types();

    auto parse_error = decode_json<JsonTestPayload>("{");
    REQUIRE_FALSE(parse_error);
    REQUIRE(parse_error.error() == "Failed to parse JSON text");

    auto encode_failure = encode_json(Ref {});
    REQUIRE_FALSE(encode_failure);
    REQUIRE(
        encode_failure.error() ==
        "Serialization failed at $: Cannot serialize an empty Ref"
    );
}

TEST_CASE(
    "DevTools JSON adapter rejects non-wire object shapes",
    "[devtools][json]"
) {
    register_json_test_types();

    auto unknown = decode_json<JsonTestPayload>(
        R"({"count":7,"mode":"Active","nested":{"enabled":true,"extra":1}})"
    );
    REQUIRE_FALSE(unknown);
    REQUIRE(
        unknown.error() ==
        "Deserialization failed at $.nested.extra: Unknown field 'extra'"
    );

    auto missing =
        decode_json<JsonTestPayload>(R"({"count":7,"mode":"Active"})");
    REQUIRE_FALSE(missing);
    REQUIRE(
        missing.error() ==
        "Deserialization failed at $.nested: Missing required field 'nested'"
    );

    auto type_tag = decode_json<JsonTestPayload>(
        R"({"$type":"JsonTestPayload","count":7,"mode":"Active","nested":{"enabled":true}})"
    );
    REQUIRE_FALSE(type_tag);
    REQUIRE(
        type_tag.error() ==
        "Deserialization failed at $.$type: Type tag '$type' is not allowed"
    );

    auto numeric_enum = decode_json<JsonTestPayload>(
        R"({"count":7,"mode":1,"nested":{"enabled":true}})"
    );
    REQUIRE_FALSE(numeric_enum);
    REQUIRE(
        numeric_enum.error().find("Deserialization failed at $.mode: ") == 0
    );
    REQUIRE(
        numeric_enum.error().find("Expected enum string") != std::string::npos
    );
}
