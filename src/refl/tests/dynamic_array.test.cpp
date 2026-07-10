#include "refl/dynamic_array.hpp"

#include "base/result.hpp"
#include "refl/cls.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace fei;

namespace {

struct MoveOnlyElement {
    int value;

    explicit MoveOnlyElement(int value) : value(value) {}
    MoveOnlyElement(const MoveOnlyElement&) = delete;
    MoveOnlyElement& operator=(const MoveOnlyElement&) = delete;
    MoveOnlyElement(MoveOnlyElement&& other) noexcept : value(other.value) {
        other.value = 0;
    }
    MoveOnlyElement& operator=(MoveOnlyElement&& other) noexcept {
        value = other.value;
        other.value = 0;
        return *this;
    }
};

struct CountingElement {
    static inline std::size_t copies {0};
    static inline std::size_t moves {0};
    static inline bool throw_on_copy {false};

    int value;

    explicit CountingElement(int value) : value(value) {}
    CountingElement(const CountingElement& other) : value(other.value) {
        if (throw_on_copy) {
            throw std::runtime_error("copy failed");
        }
        ++copies;
    }
    CountingElement& operator=(const CountingElement& other) {
        if (throw_on_copy) {
            throw std::runtime_error("copy assignment failed");
        }
        value = other.value;
        ++copies;
        return *this;
    }
    CountingElement(CountingElement&& other) noexcept : value(other.value) {
        other.value = 0;
        ++moves;
    }
    CountingElement& operator=(CountingElement&& other) noexcept {
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
    "DynamicArray owns runtime-typed homogeneous values",
    "[refl][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();

    auto created = DynamicArray::create(type_id<int>());
    REQUIRE(created);
    auto& array = *created;
    REQUIRE(array.empty());

    REQUIRE(array.push(make_val<int>(10)));
    REQUIRE(array.element_type() == type_id<int>());
    REQUIRE(array.size() == 1);

    int source = 20;
    REQUIRE(array.push(Ref(source)));
    source = 30;
    REQUIRE(source == 30);
    REQUIRE(array.at(1));
    REQUIRE(array.at(1)->get_const<int>() == 20);

    auto mismatch = array.push(make_val<std::string>("wrong"));
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error().kind == DynamicArrayError::Kind::TypeMismatch);
    REQUIRE(mismatch.error().expected_type == type_id<int>());
    REQUIRE(mismatch.error().actual_type == type_id<std::string>());
    REQUIRE(array.size() == 2);

    int sum = 0;
    for (std::size_t index = 0; index < array.size(); ++index) {
        sum += array.at(index)->get_const<int>() + static_cast<int>(index);
    }
    REQUIRE(sum == 31);

    DynamicArray copied = array;
    REQUIRE(copied.size() == 2);
    REQUIRE(copied.at(0)->get_const<int>() == 10);
    copied.at(0)->get<int>() = 99;
    REQUIRE(array.at(0)->get_const<int>() == 10);

    array.clear();
    REQUIRE(array.empty());
    REQUIRE(array.element_type() == type_id<int>());
}

TEST_CASE(
    "DynamicArray validates explicit element types and bounds",
    "[refl][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    auto& array_type = registry.register_type<DynamicArray>();
    REQUIRE_FALSE(array_type.default_constructible());

    auto array = DynamicArray::create(type_id<int>());
    REQUIRE(array);
    REQUIRE(array->empty());

    auto invalid = DynamicArray::create({});
    REQUIRE_FALSE(invalid);
    REQUIRE(
        invalid.error().kind == DynamicArrayError::Kind::InvalidElementType
    );

    const TypeId missing_type {
        std::string_view {"refl.dynamic_array.MissingElement"}
    };
    auto missing = DynamicArray::create(missing_type);
    REQUIRE_FALSE(missing);
    REQUIRE(
        missing.error().kind == DynamicArrayError::Kind::ElementTypeNotFound
    );

    auto out_of_range = array->at(0);
    REQUIRE_FALSE(out_of_range);
    REQUIRE(out_of_range.error().kind == DynamicArrayError::Kind::OutOfRange);
}

TEST_CASE(
    "DynamicArray replaces inserts and erases elements",
    "[refl][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();

    auto array = DynamicArray::create(type_id<int>());
    REQUIRE(array);
    REQUIRE(array->push(make_val<int>(1)));
    REQUIRE(array->push(make_val<int>(2)));
    REQUIRE(array->push(make_val<int>(3)));

    REQUIRE(array->set(1, make_val<int>(20)));
    REQUIRE(array->size() == 3);
    REQUIRE(array->at(1)->get_const<int>() == 20);

    int replacement = 10;
    REQUIRE(array->set(0, Ref(replacement)));
    replacement = 99;
    REQUIRE(replacement == 99);
    REQUIRE(array->at(0)->get_const<int>() == 10);

    REQUIRE(array->insert(0, make_val<int>(0)));
    REQUIRE(array->insert(2, make_val<int>(15)));
    REQUIRE(array->insert(array->size(), make_val<int>(30)));
    REQUIRE(array->size() == 6);
    REQUIRE(array->at(0)->get_const<int>() == 0);
    REQUIRE(array->at(1)->get_const<int>() == 10);
    REQUIRE(array->at(2)->get_const<int>() == 15);
    REQUIRE(array->at(3)->get_const<int>() == 20);
    REQUIRE(array->at(4)->get_const<int>() == 3);
    REQUIRE(array->at(5)->get_const<int>() == 30);

    REQUIRE(array->erase(0));
    REQUIRE(array->erase(1));
    REQUIRE(array->erase(array->size() - 1));
    REQUIRE(array->size() == 3);
    REQUIRE(array->at(0)->get_const<int>() == 10);
    REQUIRE(array->at(1)->get_const<int>() == 20);
    REQUIRE(array->at(2)->get_const<int>() == 3);

    auto mismatch = array->set(1, make_val<std::string>("wrong"));
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error().kind == DynamicArrayError::Kind::TypeMismatch);
    auto insert_mismatch = array->insert(1, make_val<std::string>("wrong"));
    REQUIRE_FALSE(insert_mismatch);
    REQUIRE(
        insert_mismatch.error().kind == DynamicArrayError::Kind::TypeMismatch
    );
    auto empty_insert = array->insert(1, Val {});
    REQUIRE_FALSE(empty_insert);
    REQUIRE(empty_insert.error().kind == DynamicArrayError::Kind::EmptyValue);
    auto empty_set = array->set(1, Val {});
    REQUIRE_FALSE(empty_set);
    REQUIRE(empty_set.error().kind == DynamicArrayError::Kind::EmptyValue);
    auto empty_ref_set = array->set(1, Ref {});
    REQUIRE_FALSE(empty_ref_set);
    REQUIRE(empty_ref_set.error().kind == DynamicArrayError::Kind::EmptyValue);
    auto empty_ref_insert = array->insert(1, Ref {});
    REQUIRE_FALSE(empty_ref_insert);
    REQUIRE(
        empty_ref_insert.error().kind == DynamicArrayError::Kind::EmptyValue
    );
    auto invalid_set = array->set(array->size(), make_val<int>(40));
    REQUIRE_FALSE(invalid_set);
    REQUIRE(invalid_set.error().kind == DynamicArrayError::Kind::OutOfRange);
    auto invalid_insert = array->insert(array->size() + 1, make_val<int>(40));
    REQUIRE_FALSE(invalid_insert);
    REQUIRE(invalid_insert.error().kind == DynamicArrayError::Kind::OutOfRange);
    auto invalid_erase = array->erase(array->size());
    REQUIRE_FALSE(invalid_erase);
    REQUIRE(invalid_erase.error().kind == DynamicArrayError::Kind::OutOfRange);

    REQUIRE(array->size() == 3);
    REQUIRE(array->at(0)->get_const<int>() == 10);
    REQUIRE(array->at(1)->get_const<int>() == 20);
    REQUIRE(array->at(2)->get_const<int>() == 3);
    REQUIRE(array->element_type() == type_id<int>());
}

TEST_CASE(
    "DynamicArray mutation preserves schemas and safely copies borrowed values",
    "[refl][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();

    auto created = DynamicArray::create(type_id<int>());
    REQUIRE(created);
    auto& array = *created;

    auto invalid_insert = array.insert(1, make_val<int>(1));
    REQUIRE_FALSE(invalid_insert);
    REQUIRE(invalid_insert.error().kind == DynamicArrayError::Kind::OutOfRange);

    auto empty_insert = array.insert(0, Val {});
    REQUIRE_FALSE(empty_insert);
    REQUIRE(empty_insert.error().kind == DynamicArrayError::Kind::EmptyValue);

    REQUIRE(array.insert(0, make_val<int>(1)));
    REQUIRE(array.element_type() == type_id<int>());
    REQUIRE(array.push(make_val<int>(2)));
    REQUIRE(array.push(make_val<int>(3)));

    auto borrowed_last = array.at(2);
    REQUIRE(borrowed_last);
    REQUIRE(array.insert(0, *borrowed_last));
    REQUIRE(array.at(0)->get_const<int>() == 3);

    auto borrowed_second = array.at(1);
    REQUIRE(borrowed_second);
    REQUIRE(array.set(0, *borrowed_second));
    REQUIRE(array.at(0)->get_const<int>() == 1);
    REQUIRE(array.at(1)->get_const<int>() == 1);

    auto borrowed_self = array.at(0);
    REQUIRE(borrowed_self);
    REQUIRE(array.set(0, *borrowed_self));
    REQUIRE(array.at(0)->get_const<int>() == 1);

    while (!array.empty()) {
        REQUIRE(array.erase(array.size() - 1));
    }
    REQUIRE(array.element_type() == type_id<int>());

    auto set_empty = array.set(0, make_val<int>(1));
    REQUIRE_FALSE(set_empty);
    REQUIRE(set_empty.error().kind == DynamicArrayError::Kind::OutOfRange);
    auto erase_empty = array.erase(0);
    REQUIRE_FALSE(erase_empty);
    REQUIRE(erase_empty.error().kind == DynamicArrayError::Kind::OutOfRange);
    REQUIRE(array.element_type() == type_id<int>());
}

TEST_CASE(
    "DynamicArray rejects move-only reflected values",
    "[refl][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<MoveOnlyElement>();

    auto rejected = DynamicArray::create(type_id<MoveOnlyElement>());
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().kind == DynamicArrayError::Kind::ValueNotStorable);
}

TEST_CASE(
    "DynamicArray mutations do not copy retained payloads",
    "[refl][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<CountingElement>();

    auto array = DynamicArray::create(type_id<CountingElement>());
    REQUIRE(array);
    REQUIRE(array->push(make_val<CountingElement>(1)));
    REQUIRE(array->push(make_val<CountingElement>(2)));
    REQUIRE(array->push(make_val<CountingElement>(3)));

    CountingElement::reset_counts();
    REQUIRE(array->set(1, make_val<CountingElement>(9)));
    REQUIRE(CountingElement::copies == 0);
    REQUIRE(CountingElement::moves > 0);
    REQUIRE(array->size() == 3);
    REQUIRE(array->at(0)->get_const<CountingElement>().value == 1);
    REQUIRE(array->at(1)->get_const<CountingElement>().value == 9);
    REQUIRE(array->at(2)->get_const<CountingElement>().value == 3);

    CountingElement::reset_counts();
    REQUIRE(array->insert(1, make_val<CountingElement>(8)));
    REQUIRE(CountingElement::copies == 0);
    REQUIRE(CountingElement::moves > 0);
    REQUIRE(array->size() == 4);
    REQUIRE(array->at(0)->get_const<CountingElement>().value == 1);
    REQUIRE(array->at(1)->get_const<CountingElement>().value == 8);
    REQUIRE(array->at(2)->get_const<CountingElement>().value == 9);
    REQUIRE(array->at(3)->get_const<CountingElement>().value == 3);

    CountingElement::reset_counts();
    REQUIRE(array->erase(2));
    REQUIRE(CountingElement::copies == 0);
    REQUIRE(CountingElement::moves > 0);
    REQUIRE(array->size() == 3);
    REQUIRE(array->at(0)->get_const<CountingElement>().value == 1);
    REQUIRE(array->at(1)->get_const<CountingElement>().value == 8);
    REQUIRE(array->at(2)->get_const<CountingElement>().value == 3);

    auto throws_on_copy = [](auto&& operation) {
        CountingElement::throw_on_copy = true;
        try {
            std::forward<decltype(operation)>(operation)();
        } catch (const std::runtime_error&) {
            CountingElement::throw_on_copy = false;
            return true;
        } catch (...) {
            CountingElement::throw_on_copy = false;
            throw;
        }
        CountingElement::throw_on_copy = false;
        return false;
    };

    auto target = DynamicArray::create(type_id<int>());
    REQUIRE(target);
    REQUIRE(target->push(make_val<int>(42)));
    REQUIRE(throws_on_copy([&] {
        *target = *array;
    }));
    REQUIRE(target->element_type() == type_id<int>());
    REQUIRE(target->size() == 1);
    REQUIRE(target->at(0)->get_const<int>() == 42);

    REQUIRE(array->element_type() == type_id<CountingElement>());
    REQUIRE(array->size() == 3);
    REQUIRE(array->at(0)->get_const<CountingElement>().value == 1);
    REQUIRE(array->at(1)->get_const<CountingElement>().value == 8);
    REQUIRE(array->at(2)->get_const<CountingElement>().value == 3);
}

TEST_CASE(
    "DynamicArray stores dynamic structs and nested arrays",
    "[refl][dynamic-array]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();
    registry.register_type<DynamicArray>();

    const TypeId item_type {
        std::string_view {"refl.dynamic_array.RuntimeItem"}
    };
    auto item_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic_array.RuntimeItem",
            .id = item_type,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<int>(),
                },
                DynamicFieldDesc {
                    .name = "label",
                    .type = type_id<std::string>(),
                },
            },
        }
    );
    REQUIRE(item_registration);

    auto& item_cls = registry.get_cls(item_type);
    auto& value_property = item_cls.get_property("value");
    auto& label_property = item_cls.get_property("label");

    auto items = DynamicArray::create(item_type);
    REQUIRE(items);
    Val item = Val::default_construct(*item_registration);
    int field_value = 7;
    std::string item_label = "first dynamic array item with owned storage";
    REQUIRE(value_property.set(item.ref(), Ref(field_value)));
    REQUIRE(label_property.set(item.ref(), Ref(item_label)));
    REQUIRE(items->push(std::move(item)));

    auto stored_item = items->at(0);
    REQUIRE(stored_item);
    auto stored_field = value_property.get(*stored_item);
    REQUIRE(stored_field);
    REQUIRE(stored_field->get_const<int>() == 7);
    auto stored_label = label_property.get(*stored_item);
    REQUIRE(stored_label);
    REQUIRE(stored_label->get_const<std::string>() == item_label);

    Val replacement = Val::default_construct(*item_registration);
    int replacement_value = 9;
    std::string replacement_label =
        "replacement dynamic array item with owned storage";
    REQUIRE(value_property.set(replacement.ref(), Ref(replacement_value)));
    REQUIRE(label_property.set(replacement.ref(), Ref(replacement_label)));
    REQUIRE(items->insert(0, replacement.ref()));
    replacement_value = 11;
    std::string changed_label = "changed outside the dynamic array";
    REQUIRE(value_property.set(replacement.ref(), Ref(replacement_value)));
    REQUIRE(label_property.set(replacement.ref(), Ref(changed_label)));
    auto inserted_item = items->at(0);
    REQUIRE(inserted_item);
    auto inserted_field = value_property.get(*inserted_item);
    REQUIRE(inserted_field);
    REQUIRE(inserted_field->get_const<int>() == 9);
    auto inserted_label = label_property.get(*inserted_item);
    REQUIRE(inserted_label);
    REQUIRE(inserted_label->get_const<std::string>() == replacement_label);

    REQUIRE(items->set(1, *inserted_item));
    REQUIRE(items->erase(0));
    REQUIRE(items->size() == 1);
    auto replaced_item = items->at(0);
    REQUIRE(replaced_item);
    auto replaced_field = value_property.get(*replaced_item);
    REQUIRE(replaced_field);
    REQUIRE(replaced_field->get_const<int>() == 9);
    auto replaced_label = label_property.get(*replaced_item);
    REQUIRE(replaced_label);
    REQUIRE(replaced_label->get_const<std::string>() == replacement_label);

    const TypeId owner_type {
        std::string_view {"refl.dynamic_array.RuntimeOwner"}
    };
    auto default_items = DynamicArray::create(item_type);
    REQUIRE(default_items);
    auto owner_registration = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic_array.RuntimeOwner",
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
    auto& items_property = registry.get_cls(owner_type).get_property("items");
    auto default_items_ref = items_property.get(owner.ref());
    REQUIRE(default_items_ref);
    REQUIRE(default_items_ref->get<DynamicArray>().element_type() == item_type);
    REQUIRE(default_items_ref->get<DynamicArray>().empty());
    REQUIRE(items_property.set(owner.ref(), Ref(*items)));
    auto stored_items_ref = items_property.get(owner.ref());
    REQUIRE(stored_items_ref);
    auto& stored_items = stored_items_ref->get<DynamicArray>();
    REQUIRE(stored_items.element_type() == item_type);
    REQUIRE(stored_items.size() == 1);
    items->clear();
    REQUIRE(stored_items.size() == 1);

    auto inner = DynamicArray::create(type_id<int>());
    REQUIRE(inner);
    REQUIRE(inner->push(make_val<int>(3)));

    auto outer = DynamicArray::create(type_id<DynamicArray>());
    REQUIRE(outer);
    REQUIRE(outer->push(make_val<DynamicArray>(std::move(*inner))));
    REQUIRE(outer->at(0));
    auto& stored_inner = outer->at(0)->get<DynamicArray>();
    REQUIRE(stored_inner.element_type() == type_id<int>());
    REQUIRE(stored_inner.at(0)->get_const<int>() == 3);
}
