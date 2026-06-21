#include "refl/registry.hpp"

#include "refl/cls.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

using namespace fei;

namespace {

struct RegistryTryType {
    int value {0};
};

struct RegistryTryClass {
    int value {0};
};

struct RegistryMissingType {
    int value {0};
};

struct RegistryMissingClass {
    int value {0};
};

enum class RegistryTryEnum { Value };

enum class RegistryMissingEnum { Value };

} // namespace

TEST_CASE("Registry try_get returns metadata references", "[refl][registry]") {
    Registry& registry = Registry::instance();

    Type& type = registry.register_type<RegistryTryType>();
    auto type_result = registry.try_get_type<RegistryTryType>();
    REQUIRE(type_result);
    REQUIRE(&*type_result == &type);

    Cls& cls = registry.register_cls<RegistryTryClass>();
    auto cls_result = registry.try_get_cls<RegistryTryClass>();
    REQUIRE(cls_result);
    REQUIRE(&*cls_result == &cls);

    Enum& enm = registry.register_enum<RegistryTryEnum>();
    auto enum_result = registry.try_get_enum<RegistryTryEnum>();
    REQUIRE(enum_result);
    REQUIRE(&*enum_result == &enm);
}

TEST_CASE("Registry try_get reports missing metadata", "[refl][registry]") {
    Registry& registry = Registry::instance();

    TypeId missing_type(std::string_view("missing.registry.type"));
    auto type_result = registry.try_get_type(missing_type);
    REQUIRE_FALSE(type_result);
    REQUIRE(type_result.error().kind == RegistryError::Kind::TypeNotFound);
    REQUIRE(type_result.error().type_id == missing_type);
    REQUIRE_FALSE(type_result.error().type_name.has_value());
    REQUIRE(type_result.error().message.contains("Type not found"));
    REQUIRE(
        type_result.error().message.contains(std::to_string(missing_type.id()))
    );

    auto templated_type_result = registry.try_get_type<RegistryMissingType>();
    REQUIRE_FALSE(templated_type_result);
    REQUIRE(
        templated_type_result.error().kind == RegistryError::Kind::TypeNotFound
    );
    REQUIRE(templated_type_result.error().type_name.has_value());
    REQUIRE(
        *templated_type_result.error().type_name ==
        std::string(type_name<RegistryMissingType>())
    );
    REQUIRE(templated_type_result.error().message.contains(
        std::string(type_name<RegistryMissingType>())
    ));

    TypeId missing_class(std::string_view("missing.registry.class"));
    auto cls_result = registry.try_get_cls(missing_class);
    REQUIRE_FALSE(cls_result);
    REQUIRE(cls_result.error().kind == RegistryError::Kind::ClassNotFound);
    REQUIRE(cls_result.error().type_id == missing_class);
    REQUIRE_FALSE(cls_result.error().type_name.has_value());
    REQUIRE(cls_result.error().message.contains("Class not found"));

    registry.register_type<RegistryMissingClass>();
    auto named_cls_result =
        registry.try_get_cls(type_id<RegistryMissingClass>());
    REQUIRE_FALSE(named_cls_result);
    REQUIRE(named_cls_result.error().type_name.has_value());
    REQUIRE(
        *named_cls_result.error().type_name ==
        std::string(type_name<RegistryMissingClass>())
    );
    REQUIRE(named_cls_result.error().message.contains(
        std::string(type_name<RegistryMissingClass>())
    ));

    TypeId missing_enum(std::string_view("missing.registry.enum"));
    auto enum_result = registry.try_get_enum(missing_enum);
    REQUIRE_FALSE(enum_result);
    REQUIRE(enum_result.error().kind == RegistryError::Kind::EnumNotFound);
    REQUIRE(enum_result.error().type_id == missing_enum);
    REQUIRE_FALSE(enum_result.error().type_name.has_value());
    REQUIRE(enum_result.error().message.contains("Enum not found"));

    registry.register_type<RegistryMissingEnum>();
    auto named_enum_result =
        registry.try_get_enum(type_id<RegistryMissingEnum>());
    REQUIRE_FALSE(named_enum_result);
    REQUIRE(named_enum_result.error().type_name.has_value());
    REQUIRE(
        *named_enum_result.error().type_name ==
        std::string(type_name<RegistryMissingEnum>())
    );
    REQUIRE(named_enum_result.error().message.contains(
        std::string(type_name<RegistryMissingEnum>())
    ));
}
