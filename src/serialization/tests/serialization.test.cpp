#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::serialization;

namespace {

enum class TestMode {
    Idle = 0,
    Running = 2,
};

struct NestedValue {
    int x {1};
    float y {2.0f};
};

struct RootValue {
    bool enabled {false};
    int count {1};
    std::uint64_t seed {2};
    double ratio {3.0};
    std::string name {"default"};
    TestMode mode {TestMode::Idle};
    NestedValue nested {};
};

struct UnsupportedValue {
    std::vector<int> values;
};

void register_test_types() {
    static bool registered = false;
    if (registered) {
        return;
    }

    auto& registry = Registry::instance();
    registry.register_enum<TestMode>()
        .add_enumerator("Idle", static_cast<std::int64_t>(TestMode::Idle))
        .add_enumerator(
            "Running",
            static_cast<std::int64_t>(TestMode::Running)
        );
    registry.register_cls<NestedValue>()
        .add_property("x", &NestedValue::x)
        .add_property("y", &NestedValue::y);
    registry.register_cls<RootValue>()
        .add_property("enabled", &RootValue::enabled)
        .add_property("count", &RootValue::count)
        .add_property("seed", &RootValue::seed)
        .add_property("ratio", &RootValue::ratio)
        .add_property("name", &RootValue::name)
        .add_property("mode", &RootValue::mode)
        .add_property("nested", &RootValue::nested);
    registry.register_cls<UnsupportedValue>().add_property(
        "values",
        &UnsupportedValue::values
    );

    registered = true;
}

} // namespace

TEST_CASE("SerializedNode round trips through JSON", "[serialization][json]") {
    SerializedNode node = SerializedNode::object({
        SerializedField {
            .name = "name",
            .value = SerializedNode::string("example"),
        },
        SerializedField {
            .name = "values",
            .value = SerializedNode::array({
                SerializedNode::signed_integer(-2),
                SerializedNode::unsigned_integer(4),
                SerializedNode::floating(1.5),
            }),
        },
    });

    auto text = write_json(node, 2);
    REQUIRE(text);
    REQUIRE(text->find("\"name\"") < text->find("\"values\""));

    auto parsed = read_json(*text);
    REQUIRE(parsed);
    auto object = parsed->try_object();
    REQUIRE(object != nullptr);
    REQUIRE(object->size() == 2);
    REQUIRE((*object)[0].name == "name");
    REQUIRE(*(*object)[0].value.try_string() == "example");
}

TEST_CASE(
    "Reflection serializer round trips reflected objects",
    "[serialization][reflection]"
) {
    register_test_types();

    RootValue root {
        .enabled = true,
        .count = -7,
        .seed = 99,
        .ratio = 0.25,
        .name = "root",
        .mode = TestMode::Running,
        .nested = NestedValue {
            .x = 42,
            .y = 4.5f,
        },
    };

    auto node = serialize(Ref(root));
    REQUIRE(node);

    auto text = write_json(*node, 2);
    REQUIRE(text);
    REQUIRE(text->find("\"mode\": \"Running\"") != std::string::npos);
    REQUIRE(text->find("\"nested\"") != std::string::npos);

    auto parsed = read_json(*text);
    REQUIRE(parsed);

    auto value = deserialize(type_id<RootValue>(), *parsed);
    REQUIRE(value);

    const auto& round_trip = value->get<RootValue>();
    REQUIRE(round_trip.enabled);
    REQUIRE(round_trip.count == -7);
    REQUIRE(round_trip.seed == 99);
    REQUIRE(round_trip.ratio == 0.25);
    REQUIRE(round_trip.name == "root");
    REQUIRE(round_trip.mode == TestMode::Running);
    REQUIRE(round_trip.nested.x == 42);
    REQUIRE(round_trip.nested.y == 4.5f);
}

TEST_CASE(
    "Reflection deserializer keeps defaults for missing fields",
    "[serialization][reflection]"
) {
    register_test_types();

    SerializedNode node = SerializedNode::object({
        SerializedField {
            .name = "count",
            .value = SerializedNode::signed_integer(12),
        },
        SerializedField {
            .name = "unknown",
            .value = SerializedNode::boolean(true),
        },
    });

    auto value = deserialize(type_id<RootValue>(), node);
    REQUIRE(value);

    const auto& root = value->get<RootValue>();
    REQUIRE_FALSE(root.enabled);
    REQUIRE(root.count == 12);
    REQUIRE(root.seed == 2);
    REQUIRE(root.ratio == 3.0);
    REQUIRE(root.name == "default");
    REQUIRE(root.mode == TestMode::Idle);
    REQUIRE(root.nested.x == 1);
    REQUIRE(root.nested.y == 2.0f);
}

TEST_CASE(
    "Reflection serializer reports unsupported property types",
    "[serialization][reflection]"
) {
    register_test_types();

    UnsupportedValue value {
        .values = {1, 2, 3},
    };

    auto node = serialize(Ref(value));
    REQUIRE_FALSE(node);
    REQUIRE(node.error().kind == SerializeError::Kind::UnsupportedType);
    REQUIRE(node.error().path == "$.values");
}
