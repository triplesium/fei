#include "refl/dynamic_map.hpp"

#include "refl/cls.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace fei;

namespace dynamic_map_test {

struct CollidingKey {
    int value;

    bool operator==(const CollidingKey&) const = default;
};

} // namespace dynamic_map_test

namespace std {

template<>
struct hash<dynamic_map_test::CollidingKey> { // NOLINT
    size_t operator()(const dynamic_map_test::CollidingKey&) const { return 0; }
};

} // namespace std

namespace {

struct EqualityOnlyKey {
    int value;

    bool operator==(const EqualityOnlyKey&) const = default;
};

struct NoEqualityKey {
    int value;
};

struct MoveOnlyValue {
    std::unique_ptr<int> value;

    explicit MoveOnlyValue(int value) : value(std::make_unique<int>(value)) {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

struct CountingMappedValue {
    static inline std::size_t copies {0};
    static inline std::size_t moves {0};
    static inline bool throw_on_copy {false};

    int value;

    explicit CountingMappedValue(int value) : value(value) {}
    CountingMappedValue(const CountingMappedValue& other) : value(other.value) {
        if (throw_on_copy) {
            throw std::runtime_error("copy failed");
        }
        ++copies;
    }
    CountingMappedValue& operator=(const CountingMappedValue& other) {
        if (throw_on_copy) {
            throw std::runtime_error("copy assignment failed");
        }
        value = other.value;
        ++copies;
        return *this;
    }
    CountingMappedValue(CountingMappedValue&& other) noexcept :
        value(other.value) {
        other.value = 0;
        ++moves;
    }
    CountingMappedValue& operator=(CountingMappedValue&& other) noexcept {
        value = other.value;
        other.value = 0;
        ++moves;
        return *this;
    }

    static void reset_counts() {
        copies = 0;
        moves = 0;
    }
};

} // namespace

TEST_CASE(
    "DynamicMap owns runtime-typed key-value entries",
    "[refl][dynamic-map]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();
    registry.register_type<float>();
    auto& map_type = registry.register_type<DynamicMap>();
    REQUIRE_FALSE(map_type.default_constructible());

    auto map = DynamicMap::create(type_id<int>(), type_id<std::string>());
    REQUIRE(map);
    REQUIRE(map->key_type() == type_id<int>());
    REQUIRE(map->mapped_type() == type_id<std::string>());

    int source_key = 1;
    std::string source_value = "one";
    REQUIRE(map->insert_or_assign(Ref(source_key), Ref(source_value)));
    source_key = 2;
    source_value = "changed";
    REQUIRE(source_key == 2);

    int key = 1;
    auto found = map->find(Ref(key));
    REQUIRE(found);
    REQUIRE(found->get<std::string>() == "one");
    REQUIRE(map->contains(Ref(key)));

    REQUIRE(map->insert_or_assign(
        make_val<int>(1),
        make_val<std::string>("updated")
    ));
    REQUIRE(map->size() == 1);
    REQUIRE(map->find(Ref(key))->get<std::string>() == "updated");

    float wrong_key = 1.0F;
    auto mismatch = map->find(Ref(wrong_key));
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error().kind == DynamicMapError::Kind::KeyTypeMismatch);

    std::size_t visited = 0;
    REQUIRE(map->for_each_entry(
        [&](DynamicMapEntryRef entry,
            std::size_t index) -> Status<DynamicMapError> {
            REQUIRE(index == visited);
            REQUIRE(entry.key.is_const());
            REQUIRE_FALSE(entry.value.is_const());
            REQUIRE(entry.key.get_const<int>() == 1);
            REQUIRE(entry.value.get_const<std::string>() == "updated");
            visited = index + 1;
            return {};
        }
    ));
    REQUIRE(visited == 1);

    DynamicMap copied = *map;
    copied.find(Ref(key))->get<std::string>() = "copy";
    REQUIRE(map->find(Ref(key))->get_const<std::string>() == "updated");
    REQUIRE(copied.find(Ref(key))->get_const<std::string>() == "copy");

    auto assigned = DynamicMap::create(type_id<float>(), type_id<int>());
    REQUIRE(assigned);
    *assigned = *map;
    REQUIRE(assigned->key_type() == type_id<int>());
    REQUIRE(assigned->mapped_type() == type_id<std::string>());
    REQUIRE(assigned->find(Ref(key))->get_const<std::string>() == "updated");

    const DynamicMap& const_map = *map;
    auto const_found = const_map.find(Ref(key));
    REQUIRE(const_found);
    REQUIRE(const_found->is_const());

    REQUIRE(map->erase(Ref(key)));
    REQUIRE(map->empty());
    REQUIRE_FALSE(map->contains(Ref(key)));
    REQUIRE(map->key_type() == type_id<int>());
    REQUIRE(map->mapped_type() == type_id<std::string>());
}

TEST_CASE("DynamicMap fixes its schema at creation", "[refl][dynamic-map]") {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();
    registry.register_type<float>();

    auto map = DynamicMap::create(type_id<int>(), type_id<std::string>());
    REQUIRE(map);
    REQUIRE(
        map->insert_or_assign(make_val<int>(3), make_val<std::string>("three"))
    );
    REQUIRE(map->key_type() == type_id<int>());
    REQUIRE(map->mapped_type() == type_id<std::string>());

    auto wrong_value =
        map->insert_or_assign(make_val<int>(4), make_val<float>(4.0F));
    REQUIRE_FALSE(wrong_value);
    REQUIRE(
        wrong_value.error().kind == DynamicMapError::Kind::MappedTypeMismatch
    );
    REQUIRE(map->size() == 1);

    map->clear();
    REQUIRE(map->empty());
    REQUIRE(map->key_type() == type_id<int>());
    REQUIRE(map->mapped_type() == type_id<std::string>());
}

TEST_CASE(
    "DynamicMap validates key and mapped value capabilities",
    "[refl][dynamic-map]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<float>();
    registry.register_type<EqualityOnlyKey>();
    registry.register_type<NoEqualityKey>();
    registry.register_type<MoveOnlyValue>();
    registry.register_type<dynamic_map_test::CollidingKey>();

    auto no_hash =
        DynamicMap::create(type_id<EqualityOnlyKey>(), type_id<int>());
    REQUIRE_FALSE(no_hash);
    REQUIRE(no_hash.error().kind == DynamicMapError::Kind::KeyNotHashable);

    auto no_equality =
        DynamicMap::create(type_id<NoEqualityKey>(), type_id<int>());
    REQUIRE_FALSE(no_equality);
    REQUIRE(
        no_equality.error().kind == DynamicMapError::Kind::KeyNotComparable
    );

    auto move_only_value =
        DynamicMap::create(type_id<int>(), type_id<MoveOnlyValue>());
    REQUIRE_FALSE(move_only_value);
    REQUIRE(
        move_only_value.error().kind ==
        DynamicMapError::Kind::MappedValueNotStorable
    );

    auto colliding = DynamicMap::create(
        type_id<dynamic_map_test::CollidingKey>(),
        type_id<int>()
    );
    REQUIRE(colliding);
    REQUIRE(colliding->insert_or_assign(
        make_val<dynamic_map_test::CollidingKey>(
            dynamic_map_test::CollidingKey {1}
        ),
        make_val<int>(10)
    ));
    REQUIRE(colliding->insert_or_assign(
        make_val<dynamic_map_test::CollidingKey>(
            dynamic_map_test::CollidingKey {2}
        ),
        make_val<int>(20)
    ));
    REQUIRE(colliding->size() == 2);
    dynamic_map_test::CollidingKey second {2};
    REQUIRE(colliding->find(Ref(second))->get_const<int>() == 20);

    auto floating = DynamicMap::create(type_id<float>(), type_id<int>());
    REQUIRE(floating);
    auto invalid_key = floating->insert_or_assign(
        make_val<float>(std::numeric_limits<float>::quiet_NaN()),
        make_val<int>(1)
    );
    REQUIRE_FALSE(invalid_key);
    REQUIRE(invalid_key.error().kind == DynamicMapError::Kind::InvalidKeyValue);
}

TEST_CASE(
    "DynamicMap replacement does not copy retained payloads",
    "[refl][dynamic-map]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<float>();
    registry.register_type<CountingMappedValue>();

    auto map =
        DynamicMap::create(type_id<int>(), type_id<CountingMappedValue>());
    REQUIRE(map);
    REQUIRE(map->insert_or_assign(
        make_val<int>(1),
        make_val<CountingMappedValue>(10)
    ));
    REQUIRE(map->insert_or_assign(
        make_val<int>(2),
        make_val<CountingMappedValue>(20)
    ));

    CountingMappedValue::reset_counts();
    REQUIRE(map->insert_or_assign(
        make_val<int>(1),
        make_val<CountingMappedValue>(30)
    ));
    REQUIRE(CountingMappedValue::copies == 0);
    REQUIRE(CountingMappedValue::moves > 0);

    int first_key = 1;
    int second_key = 2;
    REQUIRE(map->size() == 2);
    REQUIRE(
        map->find(Ref(first_key))->get_const<CountingMappedValue>().value == 30
    );
    REQUIRE(
        map->find(Ref(second_key))->get_const<CountingMappedValue>().value == 20
    );

    auto target = DynamicMap::create(type_id<float>(), type_id<int>());
    REQUIRE(target);
    REQUIRE(target->insert_or_assign(make_val<float>(4.0F), make_val<int>(40)));

    auto throws_on_copy = [&](auto&& operation) {
        CountingMappedValue::throw_on_copy = true;
        try {
            std::forward<decltype(operation)>(operation)();
        } catch (const std::runtime_error&) {
            CountingMappedValue::throw_on_copy = false;
            return true;
        } catch (...) {
            CountingMappedValue::throw_on_copy = false;
            throw;
        }
        CountingMappedValue::throw_on_copy = false;
        return false;
    };
    REQUIRE(throws_on_copy([&] {
        *target = *map;
    }));

    float target_key = 4.0F;
    REQUIRE(target->key_type() == type_id<float>());
    REQUIRE(target->mapped_type() == type_id<int>());
    REQUIRE(target->size() == 1);
    REQUIRE(target->find(Ref(target_key))->get_const<int>() == 40);

    REQUIRE(map->key_type() == type_id<int>());
    REQUIRE(map->mapped_type() == type_id<CountingMappedValue>());
    REQUIRE(map->size() == 2);
    REQUIRE(
        map->find(Ref(first_key))->get_const<CountingMappedValue>().value == 30
    );
    REQUIRE(
        map->find(Ref(second_key))->get_const<CountingMappedValue>().value == 20
    );
}

TEST_CASE(
    "DynamicMap supports dynamic struct keys and fields",
    "[refl][dynamic-map]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<DynamicMap>();

    const TypeId key_type {std::string_view {"refl.dynamic_map.RuntimeKey"}};
    auto key_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic_map.RuntimeKey",
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
    REQUIRE(key_registration->equality_comparable());
    REQUIRE(key_registration->hashable());

    Val stored_key = Val::default_construct(*key_registration);
    Val equivalent_key = Val::default_construct(*key_registration);
    int id = 7;
    auto& id_property = registry.get_cls(key_type).get_property("id");
    REQUIRE(id_property.set(stored_key.ref(), Ref(id)));
    REQUIRE(id_property.set(equivalent_key.ref(), Ref(id)));

    auto map = DynamicMap::create(key_type, type_id<int>());
    REQUIRE(map);
    REQUIRE(map->insert_or_assign(std::move(stored_key), make_val<int>(42)));
    REQUIRE(map->find(equivalent_key.ref()));
    REQUIRE(map->find(equivalent_key.ref())->get_const<int>() == 42);

    int other_id = 8;
    REQUIRE(id_property.set(equivalent_key.ref(), Ref(other_id)));
    REQUIRE_FALSE(map->contains(equivalent_key.ref()));

    const TypeId owner_type {
        std::string_view {"refl.dynamic_map.RuntimeOwner"}
    };
    auto default_values = DynamicMap::create(key_type, type_id<int>());
    REQUIRE(default_values);
    auto owner_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic_map.RuntimeOwner",
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
    auto default_values_ref = values_property.get(owner.ref());
    REQUIRE(default_values_ref);
    REQUIRE(default_values_ref->get<DynamicMap>().key_type() == key_type);
    REQUIRE(
        default_values_ref->get<DynamicMap>().mapped_type() == type_id<int>()
    );
    REQUIRE(default_values_ref->get<DynamicMap>().empty());
    REQUIRE(values_property.set(owner.ref(), Ref(*map)));
    auto stored_map_ref = values_property.get(owner.ref());
    REQUIRE(stored_map_ref);
    auto& stored_map = stored_map_ref->get<DynamicMap>();
    REQUIRE(stored_map.size() == 1);
    map->clear();
    REQUIRE(stored_map.size() == 1);
}
