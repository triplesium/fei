#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/annotations.hpp"
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

    REQUIRE(transform_2d.has_annotation<annotations::Component>());
    REQUIRE(transform_3d.has_annotation<annotations::Component>());
    REQUIRE_FALSE(transform_2d.has_annotation<annotations::Resource>());
    REQUIRE(time.has_annotation<annotations::Resource>());
    REQUIRE(fixed_time.has_annotation<annotations::Resource>());
    REQUIRE_FALSE(time.has_annotation<annotations::Component>());
    const auto time_resource = time.annotation<annotations::Resource>();
    REQUIRE(time_resource);
    REQUIRE_FALSE(time_resource->main_thread_only);

    const auto component_types =
        registry.types_with_annotation<annotations::Component>();
    REQUIRE(
        std::ranges::find(component_types, type_id<Transform2d>()) !=
        component_types.end()
    );
    REQUIRE(
        std::ranges::find(component_types, type_id<Transform3d>()) !=
        component_types.end()
    );
}
