#include "base/optional.hpp"
#include "base/result.hpp"
#include "refl/cls.hpp"
#include "refl/dynamic_array.hpp"
#include "refl/dynamic_map.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

struct ContainerValue {
    std::vector<int> numbers;
    std::array<float, 2> weights {};
    Optional<std::string> label;
    Optional<int> empty;
    std::unordered_map<std::string, int> scores;
    std::map<std::string, int> ordered_scores;
    std::set<std::string> tags;
    std::unordered_set<int> flags;
};

struct ProductValue {
    std::pair<int, std::string> pair;
    std::tuple<int, float, std::string> tuple;
};

struct UnsupportedValue {
    Result<int, int> result;
};

struct ImmovableValue {
    int value {0};

    ImmovableValue() = default;
    ImmovableValue(const ImmovableValue&) = delete;
    ImmovableValue& operator=(const ImmovableValue&) = delete;
    ImmovableValue(ImmovableValue&&) = delete;
    ImmovableValue& operator=(ImmovableValue&&) = delete;
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
    registry.register_cls<ContainerValue>()
        .add_property("numbers", &ContainerValue::numbers)
        .add_property("weights", &ContainerValue::weights)
        .add_property("label", &ContainerValue::label)
        .add_property("empty", &ContainerValue::empty)
        .add_property("scores", &ContainerValue::scores)
        .add_property("ordered_scores", &ContainerValue::ordered_scores)
        .add_property("tags", &ContainerValue::tags)
        .add_property("flags", &ContainerValue::flags);
    registry.register_cls<ProductValue>()
        .add_property("pair", &ProductValue::pair)
        .add_property("tuple", &ProductValue::tuple);
    registry.register_cls<UnsupportedValue>().add_property(
        "result",
        &UnsupportedValue::result
    );

    registered = true;
}

template<class T>
void require_optional_round_trip(const T& expected) {
    auto node = serialize(Ref(expected));
    REQUIRE(node);

    auto direct = deserialize(type_id<T>(), *node);
    REQUIRE(direct);
    REQUIRE(direct->get<T>() == expected);

    auto text = write_json(*node, 2);
    REQUIRE(text);
    auto parsed = read_json(*text);
    REQUIRE(parsed);

    auto from_json = deserialize(type_id<T>(), *parsed);
    REQUIRE(from_json);
    REQUIRE(from_json->get<T>() == expected);
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
    "Optional serialization preserves nested engagement states",
    "[serialization][reflection][optional]"
) {
    using MaybeInt = Optional<int>;
    using NestedMaybeInt = Optional<MaybeInt>;
    using DeepMaybeInt = Optional<NestedMaybeInt>;

    auto& registry = Registry::instance();
    registry.register_type<MaybeInt>();
    registry.register_type<NestedMaybeInt>();
    registry.register_type<DeepMaybeInt>();

    const std::array<MaybeInt, 2> one_layer {
        MaybeInt {nullopt},
        MaybeInt {42},
    };
    for (const auto& expected : one_layer) {
        require_optional_round_trip(expected);
    }

    const std::array<NestedMaybeInt, 3> two_layers {
        NestedMaybeInt {nullopt},
        NestedMaybeInt {MaybeInt {nullopt}},
        NestedMaybeInt {MaybeInt {42}},
    };
    for (const auto& expected : two_layers) {
        require_optional_round_trip(expected);
    }

    const std::array<DeepMaybeInt, 4> three_layers {
        DeepMaybeInt {nullopt},
        DeepMaybeInt {NestedMaybeInt {nullopt}},
        DeepMaybeInt {NestedMaybeInt {MaybeInt {nullopt}}},
        DeepMaybeInt {NestedMaybeInt {MaybeInt {42}}},
    };
    for (const auto& expected : three_layers) {
        require_optional_round_trip(expected);
    }

    auto engaged_empty = serialize(Ref(two_layers[1]));
    REQUIRE(engaged_empty);
    const auto* outer_wrapper = engaged_empty->try_object();
    REQUIRE(outer_wrapper != nullptr);
    REQUIRE(outer_wrapper->size() == 1);
    REQUIRE(outer_wrapper->front().name == "$optional");
    REQUIRE(outer_wrapper->front().value.is_null());

    auto engaged_value = serialize(Ref(two_layers[2]));
    REQUIRE(engaged_value);
    outer_wrapper = engaged_value->try_object();
    REQUIRE(outer_wrapper != nullptr);
    REQUIRE(outer_wrapper->size() == 1);
    const auto* inner_wrapper = outer_wrapper->front().value.try_object();
    REQUIRE(inner_wrapper != nullptr);
    REQUIRE(inner_wrapper->size() == 1);
    REQUIRE(inner_wrapper->front().name == "$optional");
    REQUIRE(*inner_wrapper->front().value.try_signed_integer() == 42);
}

TEST_CASE(
    "Optional deserialization accepts legacy values and rejects malformed "
    "wrappers",
    "[serialization][reflection][optional]"
) {
    using MaybeInt = Optional<int>;
    using NestedMaybeInt = Optional<MaybeInt>;

    auto& registry = Registry::instance();
    registry.register_type<MaybeInt>();
    registry.register_type<NestedMaybeInt>();

    auto legacy =
        deserialize(type_id<MaybeInt>(), SerializedNode::signed_integer(7));
    REQUIRE(legacy);
    REQUIRE(legacy->get<MaybeInt>() == MaybeInt {7});

    auto nested_legacy = deserialize(
        type_id<NestedMaybeInt>(),
        SerializedNode::signed_integer(7)
    );
    REQUIRE(nested_legacy);
    REQUIRE(
        nested_legacy->get<NestedMaybeInt>() == NestedMaybeInt {MaybeInt {7}}
    );

    auto extra_field = deserialize(
        type_id<MaybeInt>(),
        SerializedNode::object({
            SerializedField {
                .name = "$optional",
                .value = SerializedNode::signed_integer(7),
            },
            SerializedField {
                .name = "extra",
                .value = SerializedNode::boolean(true),
            },
        })
    );
    REQUIRE_FALSE(extra_field);
    REQUIRE(extra_field.error().kind == DeserializeError::Kind::InvalidNode);
    REQUIRE(extra_field.error().path == "$");

    auto duplicate_field = deserialize(
        type_id<MaybeInt>(),
        SerializedNode::object({
            SerializedField {
                .name = "$optional",
                .value = SerializedNode::signed_integer(7),
            },
            SerializedField {
                .name = "$optional",
                .value = SerializedNode::signed_integer(8),
            },
        })
    );
    REQUIRE_FALSE(duplicate_field);
    REQUIRE(
        duplicate_field.error().kind == DeserializeError::Kind::InvalidNode
    );
    REQUIRE(duplicate_field.error().path == "$");

    auto malformed_json = read_json(R"({"$optional": 7, "extra": true})");
    REQUIRE(malformed_json);
    auto malformed_json_value =
        deserialize(type_id<MaybeInt>(), *malformed_json);
    REQUIRE_FALSE(malformed_json_value);
    REQUIRE(
        malformed_json_value.error().kind == DeserializeError::Kind::InvalidNode
    );
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
    "Reflection serializer round trips reflected containers",
    "[serialization][reflection]"
) {
    register_test_types();

    ContainerValue source {
        .numbers = {1, 2, 3},
        .weights = {0.5F, 1.5F},
        .label = Optional<std::string> {"ready"},
        .empty = nullopt,
        .scores = {{"one", 1}, {"two", 2}},
        .ordered_scores = {{"first", 10}, {"second", 20}},
        .tags = {"player", "visible"},
        .flags = {4, 8},
    };

    auto node = serialize(Ref(source));
    REQUIRE(node);

    auto text = write_json(*node, 2);
    REQUIRE(text);
    REQUIRE(text->find("\"numbers\"") != std::string::npos);
    REQUIRE(text->find("\"$optional\": \"ready\"") != std::string::npos);
    REQUIRE(text->find("\"empty\": null") != std::string::npos);
    REQUIRE(text->find("\"ordered_scores\"") != std::string::npos);
    REQUIRE(text->find("\"tags\"") != std::string::npos);
    REQUIRE(text->find("\"player\"") != std::string::npos);
    REQUIRE(text->find("\"key\"") != std::string::npos);
    REQUIRE(text->find("\"value\"") != std::string::npos);

    auto parsed = read_json(*text);
    REQUIRE(parsed);

    auto value = deserialize(type_id<ContainerValue>(), *parsed);
    REQUIRE(value);

    const auto& round_trip = value->get<ContainerValue>();
    REQUIRE(round_trip.numbers == std::vector<int> {1, 2, 3});
    REQUIRE(round_trip.weights[0] == 0.5F);
    REQUIRE(round_trip.weights[1] == 1.5F);
    REQUIRE(round_trip.label.has_value());
    REQUIRE(*round_trip.label == "ready");
    REQUIRE_FALSE(round_trip.empty.has_value());
    REQUIRE(round_trip.scores.at("one") == 1);
    REQUIRE(round_trip.scores.at("two") == 2);
    REQUIRE(round_trip.ordered_scores.at("first") == 10);
    REQUIRE(round_trip.ordered_scores.at("second") == 20);
    REQUIRE(round_trip.tags.contains("player"));
    REQUIRE(round_trip.tags.contains("visible"));
    REQUIRE(round_trip.flags.contains(4));
    REQUIRE(round_trip.flags.contains(8));
}

TEST_CASE(
    "Reflection serializer round trips reflected products",
    "[serialization][reflection]"
) {
    register_test_types();

    ProductValue source {
        .pair = {7, "seven"},
        .tuple = {3, 1.5F, "tuple"},
    };

    auto node = serialize(Ref(source));
    REQUIRE(node);

    auto text = write_json(*node, 2);
    REQUIRE(text);
    REQUIRE(text->find("\"pair\"") != std::string::npos);
    REQUIRE(text->find("\"tuple\"") != std::string::npos);

    auto parsed = read_json(*text);
    REQUIRE(parsed);

    auto value = deserialize(type_id<ProductValue>(), *parsed);
    REQUIRE(value);

    const auto& round_trip = value->get<ProductValue>();
    REQUIRE(round_trip.pair.first == 7);
    REQUIRE(round_trip.pair.second == "seven");
    REQUIRE(std::get<0>(round_trip.tuple) == 3);
    REQUIRE(std::get<1>(round_trip.tuple) == 1.5F);
    REQUIRE(std::get<2>(round_trip.tuple) == "tuple");
}

TEST_CASE(
    "Reflection serializer preserves DynamicArray runtime schemas",
    "[serialization][reflection][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<DynamicArray>();

    auto source = DynamicArray::create(type_id<int>());
    REQUIRE(source);
    REQUIRE(source->push(make_val<int>(4)));
    REQUIRE(source->push(make_val<int>(8)));
    REQUIRE(source->set(0, make_val<int>(2)));
    REQUIRE(source->insert(1, make_val<int>(6)));
    REQUIRE(source->erase(2));

    auto node = serialize(Ref(*source));
    REQUIRE(node);
    auto object = node->try_object();
    REQUIRE(object != nullptr);
    auto element_type_id_field = find_field(*object, "$elementTypeId");
    auto element_type_field = find_field(*object, "$elementType");
    REQUIRE(element_type_id_field != nullptr);
    REQUIRE(element_type_field != nullptr);
    REQUIRE(
        *element_type_id_field->value.try_unsigned_integer() ==
        type_id<int>().id()
    );
    REQUIRE(
        *element_type_field->value.try_string() ==
        registry.get_type<int>().name()
    );

    auto value = deserialize(type_id<DynamicArray>(), *node);
    REQUIRE(value);
    auto& round_trip = value->get<DynamicArray>();
    REQUIRE(round_trip.element_type() == type_id<int>());
    REQUIRE(round_trip.size() == 2);
    REQUIRE(round_trip.at(0)->get_const<int>() == 2);
    REQUIRE(round_trip.at(1)->get_const<int>() == 6);

    SerializedNode legacy_schema_node = SerializedNode::object({
        SerializedField {
            .name = "$elementType",
            .value = SerializedNode::string(registry.get_type<int>().name()),
        },
        SerializedField {
            .name = "values",
            .value = SerializedNode::array({
                SerializedNode::signed_integer(9),
            }),
        },
    });
    auto legacy_value =
        deserialize(type_id<DynamicArray>(), legacy_schema_node);
    REQUIRE(legacy_value);
    REQUIRE(legacy_value->get<DynamicArray>().element_type() == type_id<int>());
    REQUIRE(legacy_value->get<DynamicArray>().at(0)->get_const<int>() == 9);

    registry.register_type<float>();
    SerializedNode mismatched_schema_node = *node;
    auto* mismatched_object = mismatched_schema_node.try_object();
    REQUIRE(mismatched_object != nullptr);
    auto* mismatched_type_id = find_field(*mismatched_object, "$elementTypeId");
    REQUIRE(mismatched_type_id != nullptr);
    mismatched_type_id->value =
        SerializedNode::unsigned_integer(type_id<float>().id());
    auto mismatched_value =
        deserialize(type_id<DynamicArray>(), mismatched_schema_node);
    REQUIRE_FALSE(mismatched_value);
    REQUIRE(
        mismatched_value.error().kind == DeserializeError::Kind::InvalidNode
    );
    REQUIRE(mismatched_value.error().path == "$.$elementType");

    const std::string left_name =
        "serialization::left::AmbiguousRuntimeElement";
    const std::string right_name =
        "serialization::right::AmbiguousRuntimeElement";
    registry.register_type(
        TypeId(std::string_view {left_name}),
        left_name,
        0,
        0,
        {}
    );
    registry.register_type(
        TypeId(std::string_view {right_name}),
        right_name,
        0,
        0,
        {}
    );
    SerializedNode ambiguous_legacy_schema = SerializedNode::object({
        SerializedField {
            .name = "$elementType",
            .value = SerializedNode::string("AmbiguousRuntimeElement"),
        },
        SerializedField {
            .name = "values",
            .value = SerializedNode::array({}),
        },
    });
    auto ambiguous_legacy_value =
        deserialize(type_id<DynamicArray>(), ambiguous_legacy_schema);
    REQUIRE_FALSE(ambiguous_legacy_value);
    REQUIRE(
        ambiguous_legacy_value.error().kind ==
        DeserializeError::Kind::InvalidNode
    );
    REQUIRE(ambiguous_legacy_value.error().path == "$.$elementType");

    auto empty = DynamicArray::create(type_id<int>());
    REQUIRE(empty);
    REQUIRE(empty->push(make_val<int>(1)));
    REQUIRE(empty->erase(0));
    auto empty_node = serialize(Ref(*empty));
    REQUIRE(empty_node);
    auto empty_value = deserialize(type_id<DynamicArray>(), *empty_node);
    REQUIRE(empty_value);
    auto& empty_round_trip = empty_value->get<DynamicArray>();
    REQUIRE(empty_round_trip.empty());
    REQUIRE(empty_round_trip.element_type() == type_id<int>());

    SerializedNode null_schema_node = SerializedNode::object({
        SerializedField {
            .name = "$elementType",
            .value = SerializedNode::null(),
        },
        SerializedField {
            .name = "values",
            .value = SerializedNode::array({}),
        },
    });
    auto null_schema_value =
        deserialize(type_id<DynamicArray>(), null_schema_node);
    REQUIRE_FALSE(null_schema_value);
    REQUIRE(
        null_schema_value.error().kind == DeserializeError::Kind::InvalidNode
    );
}

TEST_CASE(
    "Reflection serializer round trips nested and dynamic struct arrays",
    "[serialization][reflection][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<DynamicArray>();

    const TypeId item_type {
        std::string_view {"serialization.dynamic_array.RuntimeItem"}
    };
    auto item_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "serialization.dynamic_array.RuntimeItem",
            .id = item_type,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<int>(),
                },
            },
        }
    );
    REQUIRE(item_registration);

    Val item = Val::default_construct(*item_registration);
    int field_value = 21;
    REQUIRE(registry.get_cls(item_type).get_property("value").set(
        item.ref(),
        Ref(field_value)
    ));
    auto items = DynamicArray::create(item_type);
    REQUIRE(items);
    REQUIRE(items->push(std::move(item)));

    auto items_node = serialize(Ref(*items));
    REQUIRE(items_node);
    auto items_value = deserialize(type_id<DynamicArray>(), *items_node);
    REQUIRE(items_value);
    auto& round_trip_items = items_value->get<DynamicArray>();
    auto stored_item = round_trip_items.at(0);
    REQUIRE(stored_item);
    auto stored_field =
        registry.get_cls(item_type).get_property("value").get(*stored_item);
    REQUIRE(stored_field);
    REQUIRE(stored_field->get_const<int>() == 21);

    const TypeId owner_type {
        std::string_view {"serialization.dynamic_array.RuntimeOwner"}
    };
    auto default_items = DynamicArray::create(item_type);
    REQUIRE(default_items);
    auto owner_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "serialization.dynamic_array.RuntimeOwner",
            .id = owner_type,
            .fields = {
                DynamicFieldDesc {
                    .name = "items",
                    .type = type_id<DynamicArray>(),
                    .default_value = Optional<Val> {
                        make_val<DynamicArray>(std::move(*default_items))
                    },
                },
            },
        }
    );
    REQUIRE(owner_registration);

    Val owner = Val::default_construct(*owner_registration);
    auto& owner_items = registry.get_cls(owner_type).get_property("items");
    REQUIRE(owner_items.set(owner.ref(), Ref(*items)));
    auto owner_node = serialize(owner.ref());
    REQUIRE(owner_node);
    auto owner_value = deserialize(owner_type, *owner_node);
    REQUIRE(owner_value);
    auto stored_owner_items = owner_items.get(owner_value->ref());
    REQUIRE(stored_owner_items);
    auto& owner_items_array = stored_owner_items->get<DynamicArray>();
    REQUIRE(owner_items_array.element_type() == item_type);
    REQUIRE(owner_items_array.size() == 1);

    auto inner = DynamicArray::create(type_id<int>());
    REQUIRE(inner);
    REQUIRE(inner->push(make_val<int>(5)));
    auto outer = DynamicArray::create(type_id<DynamicArray>());
    REQUIRE(outer);
    REQUIRE(outer->push(make_val<DynamicArray>(std::move(*inner))));

    auto outer_node = serialize(Ref(*outer));
    REQUIRE(outer_node);
    auto outer_value = deserialize(type_id<DynamicArray>(), *outer_node);
    REQUIRE(outer_value);
    auto& round_trip_outer = outer_value->get<DynamicArray>();
    auto& round_trip_inner = round_trip_outer.at(0)->get<DynamicArray>();
    REQUIRE(round_trip_inner.element_type() == type_id<int>());
    REQUIRE(round_trip_inner.at(0)->get_const<int>() == 5);
}

TEST_CASE(
    "Reflection serializer preserves DynamicMap runtime schemas",
    "[serialization][reflection][dynamic-map]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();
    registry.register_type<DynamicMap>();

    auto source = DynamicMap::create(type_id<int>(), type_id<std::string>());
    REQUIRE(source);
    REQUIRE(
        source->insert_or_assign(make_val<int>(1), make_val<std::string>("one"))
    );
    REQUIRE(
        source->insert_or_assign(make_val<int>(2), make_val<std::string>("two"))
    );

    auto node = serialize(Ref(*source));
    REQUIRE(node);
    auto object = node->try_object();
    REQUIRE(object != nullptr);
    auto key_type_id_field = find_field(*object, "$keyTypeId");
    auto key_type_field = find_field(*object, "$keyType");
    auto mapped_type_id_field = find_field(*object, "$mappedTypeId");
    auto mapped_type_field = find_field(*object, "$mappedType");
    REQUIRE(key_type_id_field != nullptr);
    REQUIRE(key_type_field != nullptr);
    REQUIRE(mapped_type_id_field != nullptr);
    REQUIRE(mapped_type_field != nullptr);
    REQUIRE(
        *key_type_id_field->value.try_unsigned_integer() == type_id<int>().id()
    );
    REQUIRE(
        *key_type_field->value.try_string() == registry.get_type<int>().name()
    );
    REQUIRE(
        *mapped_type_field->value.try_string() ==
        registry.get_type<std::string>().name()
    );
    REQUIRE(
        *mapped_type_id_field->value.try_unsigned_integer() ==
        type_id<std::string>().id()
    );

    auto value = deserialize(type_id<DynamicMap>(), *node);
    REQUIRE(value);
    auto& round_trip = value->get<DynamicMap>();
    REQUIRE(round_trip.key_type() == type_id<int>());
    REQUIRE(round_trip.mapped_type() == type_id<std::string>());
    REQUIRE(round_trip.size() == 2);
    int first_key = 1;
    int second_key = 2;
    REQUIRE(round_trip.find(Ref(first_key))->get_const<std::string>() == "one");
    REQUIRE(
        round_trip.find(Ref(second_key))->get_const<std::string>() == "two"
    );

    SerializedNode legacy_schema_node = SerializedNode::object({
        SerializedField {
            .name = "$keyType",
            .value = SerializedNode::string(registry.get_type<int>().name()),
        },
        SerializedField {
            .name = "$mappedType",
            .value =
                SerializedNode::string(registry.get_type<std::string>().name()),
        },
        SerializedField {
            .name = "entries",
            .value = SerializedNode::array({}),
        },
    });
    auto legacy_value = deserialize(type_id<DynamicMap>(), legacy_schema_node);
    REQUIRE(legacy_value);
    REQUIRE(legacy_value->get<DynamicMap>().key_type() == type_id<int>());
    REQUIRE(
        legacy_value->get<DynamicMap>().mapped_type() == type_id<std::string>()
    );

    registry.register_type<float>();
    SerializedNode mismatched_schema_node = *node;
    auto* mismatched_object = mismatched_schema_node.try_object();
    REQUIRE(mismatched_object != nullptr);
    auto* mismatched_type_id = find_field(*mismatched_object, "$keyTypeId");
    REQUIRE(mismatched_type_id != nullptr);
    mismatched_type_id->value =
        SerializedNode::unsigned_integer(type_id<float>().id());
    auto mismatched_value =
        deserialize(type_id<DynamicMap>(), mismatched_schema_node);
    REQUIRE_FALSE(mismatched_value);
    REQUIRE(
        mismatched_value.error().kind == DeserializeError::Kind::InvalidNode
    );
    REQUIRE(mismatched_value.error().path == "$.$keyType");

    auto empty = DynamicMap::create(type_id<int>(), type_id<std::string>());
    REQUIRE(empty);
    auto empty_node = serialize(Ref(*empty));
    REQUIRE(empty_node);
    auto empty_value = deserialize(type_id<DynamicMap>(), *empty_node);
    REQUIRE(empty_value);
    auto& empty_round_trip = empty_value->get<DynamicMap>();
    REQUIRE(empty_round_trip.empty());
    REQUIRE(empty_round_trip.key_type() == type_id<int>());
    REQUIRE(empty_round_trip.mapped_type() == type_id<std::string>());

    SerializedNode null_schema_node = SerializedNode::object({
        SerializedField {
            .name = "$keyType",
            .value = SerializedNode::null(),
        },
        SerializedField {
            .name = "$mappedType",
            .value = SerializedNode::null(),
        },
        SerializedField {
            .name = "entries",
            .value = SerializedNode::array({}),
        },
    });
    auto null_schema_value =
        deserialize(type_id<DynamicMap>(), null_schema_node);
    REQUIRE_FALSE(null_schema_value);
    REQUIRE(
        null_schema_value.error().kind == DeserializeError::Kind::InvalidNode
    );

    SerializedNode missing_mapped_schema_node = SerializedNode::object({
        SerializedField {
            .name = "$keyType",
            .value = SerializedNode::string(registry.get_type<int>().name()),
        },
        SerializedField {
            .name = "$mappedType",
            .value = SerializedNode::null(),
        },
        SerializedField {
            .name = "entries",
            .value = SerializedNode::array({}),
        },
    });
    auto missing_mapped_schema_value =
        deserialize(type_id<DynamicMap>(), missing_mapped_schema_node);
    REQUIRE_FALSE(missing_mapped_schema_value);
    REQUIRE(
        missing_mapped_schema_value.error().kind ==
        DeserializeError::Kind::InvalidNode
    );
}

TEST_CASE(
    "Reflection serializer round trips dynamic struct map keys and fields",
    "[serialization][reflection][dynamic-map]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<DynamicMap>();

    const TypeId key_type {
        std::string_view {"serialization.dynamic_map.RuntimeKey"}
    };
    auto key_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "serialization.dynamic_map.RuntimeKey",
            .id = key_type,
            .fields = {
                DynamicFieldDesc {
                    .name = "id",
                    .type = type_id<int>(),
                },
            },
        }
    );
    REQUIRE(key_registration);

    Val key = Val::default_construct(*key_registration);
    int key_id = 9;
    auto& id_property = registry.get_cls(key_type).get_property("id");
    REQUIRE(id_property.set(key.ref(), Ref(key_id)));
    auto source = DynamicMap::create(key_type, type_id<int>());
    REQUIRE(source);
    REQUIRE(source->insert_or_assign(std::move(key), make_val<int>(90)));

    const TypeId owner_type {
        std::string_view {"serialization.dynamic_map.RuntimeOwner"}
    };
    auto default_values = DynamicMap::create(key_type, type_id<int>());
    REQUIRE(default_values);
    auto owner_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "serialization.dynamic_map.RuntimeOwner",
            .id = owner_type,
            .fields = {
                DynamicFieldDesc {
                    .name = "values",
                    .type = type_id<DynamicMap>(),
                    .default_value = Optional<Val> {
                        make_val<DynamicMap>(std::move(*default_values))
                    },
                },
            },
        }
    );
    REQUIRE(owner_registration);

    Val owner = Val::default_construct(*owner_registration);
    auto& values_property = registry.get_cls(owner_type).get_property("values");
    REQUIRE(values_property.set(owner.ref(), Ref(*source)));
    auto owner_node = serialize(owner.ref());
    REQUIRE(owner_node);
    auto owner_value = deserialize(owner_type, *owner_node);
    REQUIRE(owner_value);

    auto map_ref = values_property.get(owner_value->ref());
    REQUIRE(map_ref);
    auto& round_trip = map_ref->get<DynamicMap>();
    Val equivalent_key = Val::default_construct(*key_registration);
    REQUIRE(id_property.set(equivalent_key.ref(), Ref(key_id)));
    REQUIRE(round_trip.find(equivalent_key.ref()));
    REQUIRE(round_trip.find(equivalent_key.ref())->get_const<int>() == 90);
}

TEST_CASE(
    "Reflection deserializer rejects duplicate DynamicMap keys",
    "[serialization][reflection][dynamic-map]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();
    registry.register_type<DynamicMap>();

    const auto int_name = registry.get_type<int>().name();
    const auto string_name = registry.get_type<std::string>().name();
    SerializedNode node = SerializedNode::object({
        SerializedField {
            .name = "$keyType",
            .value = SerializedNode::string(int_name),
        },
        SerializedField {
            .name = "$mappedType",
            .value = SerializedNode::string(string_name),
        },
        SerializedField {
            .name = "entries",
            .value = SerializedNode::array({
                SerializedNode::object({
                    SerializedField {
                        .name = "key",
                        .value = SerializedNode::signed_integer(1),
                    },
                    SerializedField {
                        .name = "value",
                        .value = SerializedNode::string("one"),
                    },
                }),
                SerializedNode::object({
                    SerializedField {
                        .name = "key",
                        .value = SerializedNode::signed_integer(1),
                    },
                    SerializedField {
                        .name = "value",
                        .value = SerializedNode::string("duplicate"),
                    },
                }),
            }),
        },
    });

    auto value = deserialize(type_id<DynamicMap>(), node);
    REQUIRE_FALSE(value);
    REQUIRE(value.error().kind == DeserializeError::Kind::InvalidNode);
    REQUIRE(value.error().path == "$.entries[1].key");
}

TEST_CASE(
    "Reflection serializer round trips empty fixed-size containers",
    "[serialization][reflection]"
) {
    auto& registry = Registry::instance();
    registry.register_type<std::array<int, 0>>();
    registry.register_type<std::tuple<>>();

    std::array<int, 0> empty_array {};
    auto array_node = serialize(Ref(empty_array));
    REQUIRE(array_node);
    auto array = array_node->try_array();
    REQUIRE(array != nullptr);
    REQUIRE(array->empty());

    auto array_value = deserialize(type_id<std::array<int, 0>>(), *array_node);
    REQUIRE(array_value);
    REQUIRE(array_value->get<std::array<int, 0>>().empty());

    std::tuple<> empty_tuple;
    auto tuple_node = serialize(Ref(empty_tuple));
    REQUIRE(tuple_node);
    auto tuple = tuple_node->try_array();
    REQUIRE(tuple != nullptr);
    REQUIRE(tuple->empty());

    auto tuple_value = deserialize(type_id<std::tuple<>>(), *tuple_node);
    REQUIRE(tuple_value);
    (void)tuple_value->get<std::tuple<>>();
}

TEST_CASE(
    "Reflection deserializer rejects fixed-size container length mismatches",
    "[serialization][reflection]"
) {
    register_test_types();

    auto short_pair = deserialize(
        type_id<std::pair<int, std::string>>(),
        SerializedNode::array({
            SerializedNode::signed_integer(7),
        })
    );
    REQUIRE_FALSE(short_pair);
    REQUIRE(short_pair.error().kind == DeserializeError::Kind::InvalidNode);
    REQUIRE(short_pair.error().path == "$");
    REQUIRE(
        short_pair.error().message.find("Expected 2 elements") !=
        std::string::npos
    );

    auto long_tuple = deserialize(
        type_id<std::tuple<int, float, std::string>>(),
        SerializedNode::array({
            SerializedNode::signed_integer(3),
            SerializedNode::floating(1.5),
            SerializedNode::string("tuple"),
            SerializedNode::boolean(true),
        })
    );
    REQUIRE_FALSE(long_tuple);
    REQUIRE(long_tuple.error().kind == DeserializeError::Kind::InvalidNode);
    REQUIRE(long_tuple.error().path == "$");
    REQUIRE(
        long_tuple.error().message.find("Expected 3 elements") !=
        std::string::npos
    );
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
    "Reflection deserializer heap-stores immovable reflected types",
    "[serialization][reflection]"
) {
    Registry::instance().register_cls<ImmovableValue>().add_property(
        "value",
        &ImmovableValue::value
    );

    auto value = deserialize(
        type_id<ImmovableValue>(),
        SerializedNode::object({
            SerializedField {
                .name = "value",
                .value = SerializedNode::signed_integer(12),
            },
        })
    );

    REQUIRE(value);
    REQUIRE(value->get<ImmovableValue>().value == 12);
}

TEST_CASE(
    "Reflection serializer reports unsupported property types",
    "[serialization][reflection]"
) {
    register_test_types();

    UnsupportedValue value {
        .result = Result<int, int> {1},
    };

    auto node = serialize(Ref(value));
    REQUIRE_FALSE(node);
    REQUIRE(node.error().kind == SerializeError::Kind::UnsupportedType);
    REQUIRE(node.error().path == "$.result");
}
