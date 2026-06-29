#include "refl/dynamic_type.hpp"

#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

using namespace fei;

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
}

TEST_CASE("Dynamic struct registration validates field metadata", "[refl][dynamic]") {
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
                    .type = TypeId {std::string_view {"refl.dynamic.MissingType"}},
                },
            },
        }
    );
    REQUIRE_FALSE(missing_type);
    REQUIRE(
        missing_type.error().kind ==
        DynamicTypeError::Kind::FieldTypeNotFound
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
    REQUIRE(
        second.error().kind == DynamicTypeError::Kind::TypeAlreadyExists
    );
}
