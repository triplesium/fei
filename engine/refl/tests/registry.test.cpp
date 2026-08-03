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

TEST_CASE(
    "Registry resolves exact and unique stripped type names",
    "[refl][registry]"
) {
    Registry& registry = Registry::instance();

    const std::string exact_name = "RegistryExactPreferredLookupType";
    Type& exact_type = registry.register_type(
        TypeId(std::string_view {exact_name}),
        exact_name,
        0,
        0,
        {}
    );
    const std::string qualified_exact_name =
        "registry::lookup::RegistryExactPreferredLookupType";
    registry.register_type(
        TypeId(std::string_view {qualified_exact_name}),
        qualified_exact_name,
        0,
        0,
        {}
    );

    auto exact = registry.try_get_type_exact(exact_name);
    REQUIRE(exact);
    REQUIRE(&*exact == &exact_type);

    auto exact_first = registry.try_get_type(std::string_view {exact_name});
    REQUIRE(exact_first);
    REQUIRE(&*exact_first == &exact_type);

    const std::string unique_name =
        "registry::lookup::RegistryUniqueStrippedLookupType";
    Type& unique_type = registry.register_type(
        TypeId(std::string_view {unique_name}),
        unique_name,
        0,
        0,
        {}
    );

    auto missing_exact =
        registry.try_get_type_exact("RegistryUniqueStrippedLookupType");
    REQUIRE_FALSE(missing_exact);
    REQUIRE(missing_exact.error().kind == RegistryError::Kind::TypeNotFound);

    auto unique_stripped =
        registry.try_get_type("RegistryUniqueStrippedLookupType");
    REQUIRE(unique_stripped);
    REQUIRE(&*unique_stripped == &unique_type);
}

TEST_CASE(
    "Registry rejects ambiguous stripped type names",
    "[refl][registry]"
) {
    Registry& registry = Registry::instance();

    const std::string left_name =
        "registry::left::RegistryAmbiguousStrippedLookupType";
    const std::string right_name =
        "registry::right::RegistryAmbiguousStrippedLookupType";
    Type& left_type = registry.register_type(
        TypeId(std::string_view {left_name}),
        left_name,
        0,
        0,
        {}
    );
    Type& right_type = registry.register_type(
        TypeId(std::string_view {right_name}),
        right_name,
        0,
        0,
        {}
    );

    auto left_exact = registry.try_get_type(std::string_view {left_name});
    REQUIRE(left_exact);
    REQUIRE(&*left_exact == &left_type);
    auto right_exact = registry.try_get_type(std::string_view {right_name});
    REQUIRE(right_exact);
    REQUIRE(&*right_exact == &right_type);

    auto ambiguous =
        registry.try_get_type("RegistryAmbiguousStrippedLookupType");
    REQUIRE_FALSE(ambiguous);
    REQUIRE(ambiguous.error().kind == RegistryError::Kind::AmbiguousTypeName);
    REQUIRE(ambiguous.error().type_name.has_value());
    REQUIRE(
        *ambiguous.error().type_name == "RegistryAmbiguousStrippedLookupType"
    );
    REQUIRE(ambiguous.error().message.contains("ambiguous"));
}
