#include "scripting/detail/reflection_bridge.hpp"

#include "refl/annotations.hpp"
#include "refl/cls.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>

namespace ets::scripting_test {

struct VisibleType {};
struct PreludeType {};

struct StaticFactory {
    int value {0};

    static StaticFactory make(int value) { return {.value = value}; }
};

} // namespace ets::scripting_test

using namespace ets;

TEST_CASE(
    "Luau reflection names omit the entisium root namespace",
    "[scripting][luau][reflection]"
) {
    auto& type =
        Registry::instance().register_type<scripting_test::VisibleType>(
            {"ets", "scripting_test"},
            "VisibleType"
        );

    REQUIRE(is_luau_visible(type));
    const auto name = luau_type_name(type);
    REQUIRE(name.namespace_path.size() == 1);
    CHECK(name.namespace_path.front() == "scripting_test");
    CHECK(name.local_name == "VisibleType");
    CHECK(luau_type_path(type) == "scripting_test.VisibleType");
    CHECK(luau_type_path(type, "::") == "scripting_test::VisibleType");
}

TEST_CASE(
    "ScriptPrelude reflection annotation selects visible global aliases",
    "[scripting][luau][reflection]"
) {
    auto& registry = Registry::instance();
    register_generated_reflection();
    registry.register_cls<scripting_test::PreludeType>(
        {"ets", "scripting_test"},
        "PreludeType"
    );
    registry.add_annotation<scripting_test::PreludeType>(
        annotations::ScriptPrelude {}
    );

    const auto& type = registry.get_type<scripting_test::PreludeType>();
    CHECK(is_luau_visible(type));
    CHECK(is_luau_prelude(type));
}

TEST_CASE(
    "Luau reflection invokes static methods without an instance",
    "[scripting][luau][reflection][static]"
) {
    auto& registry = Registry::instance();
    registry
        .register_cls<scripting_test::StaticFactory>(
            {"ets", "scripting_test"},
            "StaticFactory"
        )
        .add_property("value", &scripting_test::StaticFactory::value)
        .add_method("make", &scripting_test::StaticFactory::make);

    const int input = 17;
    const auto type = type_id<scripting_test::StaticFactory>();
    REQUIRE(luau_has_static_method(type, "make"));
    REQUIRE_FALSE(luau_has_static_method(type, "missing"));

    auto result = luau_invoke_static_method(type, "make", {Ref(input)});
    REQUIRE(result);
    REQUIRE(result->is_value());
    CHECK(result->value().get<scripting_test::StaticFactory>().value == 17);
}
