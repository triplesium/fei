#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/type_tags.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "Generated reflection records component and resource tags",
    "[core][refl][tag]"
) {
    register_generated_reflection();
    auto& registry = Registry::instance();

    const auto& transform_2d = registry.get_type<Transform2d>();
    const auto& transform_3d = registry.get_type<Transform3d>();
    const auto& time = registry.get_type<Time>();
    const auto& fixed_time = registry.get_type<FixedTime>();

    REQUIRE(transform_2d.has_tag(ComponentTypeTag));
    REQUIRE(transform_3d.has_tag(ComponentTypeTag));
    REQUIRE_FALSE(transform_2d.has_tag(ResourceTypeTag));
    REQUIRE(time.has_tag(ResourceTypeTag));
    REQUIRE(fixed_time.has_tag(ResourceTypeTag));
    REQUIRE_FALSE(time.has_tag(ComponentTypeTag));

    const auto component_types = registry.types_with_tag(ComponentTypeTag);
    REQUIRE(
        std::ranges::find(component_types, type_id<Transform2d>()) !=
        component_types.end()
    );
    REQUIRE(
        std::ranges::find(component_types, type_id<Transform3d>()) !=
        component_types.end()
    );
}
