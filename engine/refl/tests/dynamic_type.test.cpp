#include "refl/dynamic_type.hpp"

#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace fei;

namespace {

struct EqualityOnlyField {
    int value {0};

    bool operator==(const EqualityOnlyField&) const = default;
};

struct ConstructionTracker {
    static inline int live_count {0};

    ConstructionTracker() { ++live_count; }
    ConstructionTracker(const ConstructionTracker&) { ++live_count; }
    ConstructionTracker(ConstructionTracker&&) noexcept { ++live_count; }
    ConstructionTracker& operator=(const ConstructionTracker&) = default;
    ConstructionTracker& operator=(ConstructionTracker&&) noexcept = default;
    ~ConstructionTracker() { --live_count; }
};

struct ThrowingField {
    static inline bool throw_on_default {false};
    static inline bool throw_on_copy {false};

    int value {0};

    ThrowingField() {
        if (throw_on_default) {
            throw std::runtime_error("default construction failed");
        }
    }
    ThrowingField(const ThrowingField& other) : value(other.value) {
        if (throw_on_copy) {
            throw std::runtime_error("copy construction failed");
        }
    }
    ThrowingField(ThrowingField&& other) noexcept : value(other.value) {
        other.value = 0;
    }
    ThrowingField& operator=(const ThrowingField&) = default;
    ThrowingField& operator=(ThrowingField&&) noexcept = default;
};

struct NonDefaultField {
    int value;

    NonDefaultField() = delete;
    explicit NonDefaultField(int value) : value(value) {}
    NonDefaultField(const NonDefaultField&) = default;
    NonDefaultField& operator=(const NonDefaultField&) = default;
    NonDefaultField(NonDefaultField&&) noexcept = default;
    NonDefaultField& operator=(NonDefaultField&&) noexcept = default;
};

} // namespace

TEST_CASE(
    "Dynamic structs register reflected layout and properties",
    "[refl][dynamic]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<float>();
    registry.register_type<std::string>();

    const TypeId id {std::string_view {"refl.dynamic.Health"}};
    auto type = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.Health",
            .id = id,
            .fields = {
                DynamicFieldDesc {
                    .name = "current",
                    .type = type_id<int>(),
                    .default_value = Optional<Val> {make_val<int>(100)},
                },
                DynamicFieldDesc {
                    .name = "ratio",
                    .type = type_id<float>(),
                },
                DynamicFieldDesc {
                    .name = "label",
                    .type = type_id<std::string>(),
                    .default_value =
                        Optional<Val> {make_val<std::string>("ready")},
                },
            },
        }
    );
    REQUIRE(type);
    REQUIRE(type->id() == id);
    REQUIRE(type->default_constructible());
    REQUIRE(type->copy_constructible());
    REQUIRE(type->destructible());
    REQUIRE(type->equality_comparable());
    REQUIRE(type->hashable());

    const auto* layout = registry.try_get_dynamic_struct_layout(id);
    REQUIRE(layout != nullptr);
    REQUIRE(layout->fields.size() == 3);
    REQUIRE(layout->size >= sizeof(int) + sizeof(float));
    REQUIRE(layout->align >= alignof(std::string));

    Val value = Val::default_construct(*type);
    auto& cls = registry.get_cls(id);

    auto& current_prop = cls.get_property("current");
    auto current = current_prop.get(value.ref());
    REQUIRE(current);
    REQUIRE(current->get<int>() == 100);

    auto next_current = make_val<int>(75);
    REQUIRE(current_prop.set(value.ref(), next_current.ref()));
    current = current_prop.get(value.ref());
    REQUIRE(current);
    REQUIRE(current->get<int>() == 75);

    auto& ratio_prop = cls.get_property("ratio");
    auto ratio_value = make_val<int>(2);
    REQUIRE(ratio_prop.set(value.ref(), ratio_value.ref()));
    auto ratio = ratio_prop.get(value.ref());
    REQUIRE(ratio);
    REQUIRE(ratio->get<float>() == 2.0f);

    auto label = cls.get_property("label").get(value.ref());
    REQUIRE(label);
    REQUIRE(label->get<std::string>() == "ready");

    Val copied = value;
    current = current_prop.get(copied.ref());
    REQUIRE(current);
    REQUIRE(current->get<int>() == 75);
    label = cls.get_property("label").get(copied.ref());
    REQUIRE(label);
    REQUIRE(label->get<std::string>() == "ready");

    auto equal =
        type->equals(value.ref().const_ptr(), copied.ref().const_ptr());
    REQUIRE(equal);
    REQUIRE(*equal);
    auto value_hash = type->hash_value(value.ref().const_ptr());
    auto copied_hash = type->hash_value(copied.ref().const_ptr());
    REQUIRE(value_hash);
    REQUIRE(copied_hash);
    REQUIRE(*value_hash == *copied_hash);

    auto changed_current = make_val<int>(76);
    REQUIRE(current_prop.set(copied.ref(), changed_current.ref()));
    equal = type->equals(value.ref().const_ptr(), copied.ref().const_ptr());
    REQUIRE(equal);
    REQUIRE_FALSE(*equal);
}

TEST_CASE(
    "Dynamic struct value capabilities follow their fields",
    "[refl][dynamic]"
) {
    auto& registry = Registry::instance();
    registry.register_type<EqualityOnlyField>();

    const TypeId equality_only_id {
        std::string_view {"refl.dynamic.EqualityOnly"}
    };
    auto equality_only_type = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.EqualityOnly",
            .id = equality_only_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<EqualityOnlyField>(),
                },
            },
        }
    );
    REQUIRE(equality_only_type);
    REQUIRE(equality_only_type->equality_comparable());
    REQUIRE_FALSE(equality_only_type->hashable());

    Val lhs = Val::default_construct(*equality_only_type);
    Val rhs = Val::default_construct(*equality_only_type);
    auto equal = equality_only_type->equals(
        lhs.ref().const_ptr(),
        rhs.ref().const_ptr()
    );
    REQUIRE(equal);
    REQUIRE(*equal);
    REQUIRE_FALSE(equality_only_type->hash_value(lhs.ref().const_ptr()));

    const TypeId empty_id {std::string_view {"refl.dynamic.EmptyValue"}};
    auto empty_type = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.EmptyValue",
            .id = empty_id,
        }
    );
    REQUIRE(empty_type);
    REQUIRE(empty_type->equality_comparable());
    REQUIRE(empty_type->hashable());
    Val empty_lhs = Val::default_construct(*empty_type);
    Val empty_rhs = Val::default_construct(*empty_type);
    REQUIRE(*empty_type->equals(
        empty_lhs.ref().const_ptr(),
        empty_rhs.ref().const_ptr()
    ));
    REQUIRE(
        *empty_type->hash_value(empty_lhs.ref().const_ptr()) ==
        *empty_type->hash_value(empty_rhs.ref().const_ptr())
    );
}

TEST_CASE(
    "Dynamic struct construction rolls back completed fields on failure",
    "[refl][dynamic]"
) {
    auto& registry = Registry::instance();
    registry.register_type<ConstructionTracker>();
    registry.register_type<ThrowingField>();

    const TypeId id {std::string_view {"refl.dynamic.ThrowingConstruction"}};
    auto type = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.ThrowingConstruction",
            .id = id,
            .fields = {
                DynamicFieldDesc {
                    .name = "tracker",
                    .type = type_id<ConstructionTracker>(),
                },
                DynamicFieldDesc {
                    .name = "throwing",
                    .type = type_id<ThrowingField>(),
                },
            },
        }
    );
    REQUIRE(type);
    REQUIRE(ConstructionTracker::live_count == 0);

    auto throws_when = [](bool& enabled, auto&& operation) {
        enabled = true;
        try {
            std::forward<decltype(operation)>(operation)();
        } catch (const std::runtime_error&) {
            enabled = false;
            return true;
        } catch (...) {
            enabled = false;
            throw;
        }
        enabled = false;
        return false;
    };

    REQUIRE(throws_when(ThrowingField::throw_on_default, [&] {
        (void)Val::default_construct(*type);
    }));
    REQUIRE(ConstructionTracker::live_count == 0);

    const TypeId defaulted_id {
        std::string_view {"refl.dynamic.ThrowingDefaultValue"}
    };
    auto defaulted_type = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.ThrowingDefaultValue",
            .id = defaulted_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "tracker",
                    .type = type_id<ConstructionTracker>(),
                },
                DynamicFieldDesc {
                    .name = "throwing",
                    .type = type_id<ThrowingField>(),
                    .default_value = Optional<Val> {make_val<ThrowingField>()},
                },
            },
        }
    );
    REQUIRE(defaulted_type);
    REQUIRE(throws_when(ThrowingField::throw_on_copy, [&] {
        (void)Val::default_construct(*defaulted_type);
    }));
    REQUIRE(ConstructionTracker::live_count == 0);

    {
        Val source = Val::default_construct(*type);
        REQUIRE(ConstructionTracker::live_count == 1);

        REQUIRE(throws_when(ThrowingField::throw_on_copy, [&] {
            auto copied = Val::copy(source.ref());
            (void)copied;
        }));
        REQUIRE(ConstructionTracker::live_count == 1);
        REQUIRE(source);
    }
    REQUIRE(ConstructionTracker::live_count == 0);
}

TEST_CASE(
    "Dynamic struct fields can use explicit defaults without default "
    "constructors",
    "[refl][dynamic]"
) {
    auto& registry = Registry::instance();
    auto& field_type = registry.register_type<NonDefaultField>();
    REQUIRE_FALSE(field_type.default_constructible());

    const TypeId missing_default_id {
        std::string_view {"refl.dynamic.NonDefaultFieldMissingDefault"}
    };
    auto missing_default = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.NonDefaultFieldMissingDefault",
            .id = missing_default_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<NonDefaultField>(),
                },
            },
        }
    );
    REQUIRE_FALSE(missing_default);
    REQUIRE(
        missing_default.error().kind == DynamicTypeError::Kind::InvalidFieldType
    );

    const TypeId wrong_default_id {
        std::string_view {"refl.dynamic.NonDefaultFieldWrongDefault"}
    };
    auto wrong_default = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.NonDefaultFieldWrongDefault",
            .id = wrong_default_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<NonDefaultField>(),
                    .default_value = Optional<Val> {make_val<int>(17)},
                },
            },
        }
    );
    REQUIRE_FALSE(wrong_default);
    REQUIRE(
        wrong_default.error().kind == DynamicTypeError::Kind::InvalidFieldType
    );

    const TypeId explicit_default_id {
        std::string_view {"refl.dynamic.NonDefaultFieldExplicitDefault"}
    };
    auto explicit_default = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.NonDefaultFieldExplicitDefault",
            .id = explicit_default_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<NonDefaultField>(),
                    .default_value =
                        Optional<Val> {make_val<NonDefaultField>(17)},
                },
            },
        }
    );
    REQUIRE(explicit_default);
    REQUIRE(explicit_default->default_constructible());

    Val instance = Val::default_construct(*explicit_default);
    auto value = registry.get_cls(explicit_default_id)
                     .get_property("value")
                     .get(instance.ref());
    REQUIRE(value);
    REQUIRE(value->get_const<NonDefaultField>().value == 17);
}

TEST_CASE(
    "Dynamic struct registration validates field metadata",
    "[refl][dynamic]"
) {
    auto& registry = Registry::instance();
    registry.register_type<int>();

    const TypeId duplicate_id {
        std::string_view {"refl.dynamic.DuplicateFields"}
    };
    auto duplicate_field = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.DuplicateFields",
            .id = duplicate_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<int>(),
                },
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<int>(),
                },
            },
        }
    );
    REQUIRE_FALSE(duplicate_field);
    REQUIRE(
        duplicate_field.error().kind == DynamicTypeError::Kind::DuplicateField
    );

    auto missing_type = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.MissingFieldType",
            .id = TypeId {std::string_view {"refl.dynamic.MissingFieldType"}},
            .fields = {
                DynamicFieldDesc {
                    .name = "missing",
                    .type =
                        TypeId {std::string_view {"refl.dynamic.MissingType"}},
                },
            },
        }
    );
    REQUIRE_FALSE(missing_type);
    REQUIRE(
        missing_type.error().kind == DynamicTypeError::Kind::FieldTypeNotFound
    );

    const TypeId existing_id {std::string_view {"refl.dynamic.Existing"}};
    auto first = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.Existing",
            .id = existing_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<int>(),
                },
            },
        }
    );
    REQUIRE(first);

    auto second = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "refl.dynamic.Existing",
            .id = existing_id,
            .fields = {
                DynamicFieldDesc {
                    .name = "value",
                    .type = type_id<int>(),
                },
            },
        }
    );
    REQUIRE_FALSE(second);
    REQUIRE(second.error().kind == DynamicTypeError::Kind::TypeAlreadyExists);
}
