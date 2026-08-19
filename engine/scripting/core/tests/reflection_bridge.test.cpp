#include "scripting/reflection_bridge.hpp"

#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>

namespace fei::scripting_test {

struct VisibleType {};
struct HiddenType {};

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
