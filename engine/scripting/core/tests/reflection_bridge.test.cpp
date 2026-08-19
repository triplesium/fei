#include "scripting/reflection_bridge.hpp"

#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>

namespace fei::scripting_test {

struct VisibleType {};
struct HiddenType {};

struct StaticFactory {
    int value {0};

    static StaticFactory make(int value) { return {.value = value}; }
};

} // namespace fei::scripting_test

using namespace fei;

TEST_CASE(
    "Script reflection names omit the fei root namespace",
    "[scripting][reflection]"
) {
    auto& type =
        Registry::instance().register_type<scripting_test::VisibleType>(
            {"fei", "scripting_test"},
            "VisibleType"
        );

    REQUIRE(is_script_visible(type));
    const auto name = script_type_name(type);
    REQUIRE(name.namespace_path.size() == 1);
    CHECK(name.namespace_path.front() == "scripting_test");
    CHECK(name.local_name == "VisibleType");
    CHECK(script_type_path(type) == "scripting_test.VisibleType");
    CHECK(script_type_path(type, "::") == "scripting_test::VisibleType");
}

TEST_CASE(
    "NoScript reflection annotation hides types from scripts",
    "[scripting][reflection]"
) {
    auto& registry = Registry::instance();
    auto& type = registry.register_type<scripting_test::HiddenType>(
        {"fei", "scripting_test"},
        "HiddenType"
    );
    registry.add_generated_annotation<scripting_test::HiddenType>("NoScript");

    CHECK_FALSE(is_script_visible(type));
}

TEST_CASE(
    "Script reflection invokes static methods without an instance",
    "[scripting][reflection][static]"
) {
    auto& registry = Registry::instance();
    registry
        .register_cls<scripting_test::StaticFactory>(
            {"fei", "scripting_test"},
            "StaticFactory"
        )
        .add_property("value", &scripting_test::StaticFactory::value)
        .add_method("make", &scripting_test::StaticFactory::make);

    const int input = 17;
    const auto type = type_id<scripting_test::StaticFactory>();
    REQUIRE(script_has_static_method(type, "make"));
    REQUIRE_FALSE(script_has_static_method(type, "missing"));

    auto result = script_invoke_static_method(type, "make", {Ref(input)});
    REQUIRE(result);
    REQUIRE(result->is_value());
    CHECK(result->value().get<scripting_test::StaticFactory>().value == 17);
}
