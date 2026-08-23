#include "refl/type.hpp"

#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "test_types.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace ets;
using namespace ets::refl_test;

namespace {

struct EqualityOnly {
    int value;

    bool operator==(const EqualityOnly&) const = default;
};

struct NoEquality {
    int value;
};

struct TaggedType {
    int value;
};

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(const ThrowingMove&) = default;
    ThrowingMove& operator=(const ThrowingMove&) = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
    ThrowingMove& operator=(ThrowingMove&&) noexcept(false) { return *this; }
};

struct ThrowingMoveAssignment {
    ThrowingMoveAssignment() = default;
    ThrowingMoveAssignment(const ThrowingMoveAssignment&) = delete;
    ThrowingMoveAssignment& operator=(const ThrowingMoveAssignment&) = delete;
    ThrowingMoveAssignment(ThrowingMoveAssignment&&) noexcept = default;
    ThrowingMoveAssignment&
    operator=(ThrowingMoveAssignment&&) noexcept(false) {
        return *this;
    }
};

} // namespace

TEST_CASE("Reflection value move contracts are explicit", "[refl][type]") {
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<TestStruct>);
    STATIC_REQUIRE_FALSE(std::is_nothrow_move_constructible_v<ThrowingMove>);
    STATIC_REQUIRE(
        std::is_nothrow_move_constructible_v<ThrowingMoveAssignment>
    );

    auto& throwing_move_type =
        Registry::instance().register_type<ThrowingMove>();
    REQUIRE_FALSE(throwing_move_type.move_constructible());
    REQUIRE_FALSE(throwing_move_type.move_assignable());

    auto& type = Registry::instance().register_type<ThrowingMoveAssignment>();
    REQUIRE(type.move_constructible());
    REQUIRE_FALSE(type.move_assignable());
}

TEST_CASE("Registry records type metadata and capabilities", "[refl][type]") {
    Registry& registry = Registry::instance();

    Type& test_type = registry.register_type<TestStruct>();
    REQUIRE(test_type.id() == type_id<TestStruct>());
    REQUIRE(test_type.hash() == type_id<TestStruct>());
    REQUIRE(test_type.size() == sizeof(TestStruct));
    REQUIRE(test_type.align() == alignof(TestStruct));
    REQUIRE(test_type.default_constructible());
    REQUIRE(test_type.copy_constructible());
    REQUIRE(test_type.move_constructible());
    REQUIRE(test_type.copy_assignable());
    REQUIRE(test_type.move_assignable());
    REQUIRE(test_type.delete_func() != nullptr);
    REQUIRE(type(test_type.id()).id() == test_type.id());

    struct alignas(32) AlignedStruct {
        int value;
    };
    Type& aligned_type = registry.register_type<AlignedStruct>();
    REQUIRE(aligned_type.size() == sizeof(AlignedStruct));
    REQUIRE(aligned_type.align() == alignof(AlignedStruct));

    Type& int_type = registry.register_type<int>();
    Type& float_type = registry.register_type<float>();
    REQUIRE(int_type.is_number());
    REQUIRE(int_type.is_integral());
    REQUIRE_FALSE(int_type.is_floating_point());
    REQUIRE(float_type.is_number());
    REQUIRE_FALSE(float_type.is_integral());
    REQUIRE(float_type.is_floating_point());

    REQUIRE(int_type.equality_comparable());
    REQUIRE(int_type.hashable());
    int lhs = 42;
    int same = 42;
    int different = 7;
    REQUIRE(int_type.equals(&lhs, &same));
    REQUIRE(*int_type.equals(&lhs, &same));
    REQUIRE_FALSE(*int_type.equals(&lhs, &different));
    REQUIRE(int_type.hash_value(&lhs));
    REQUIRE(*int_type.hash_value(&lhs) == *int_type.hash_value(&same));

    Type& equality_only_type = registry.register_type<EqualityOnly>();
    REQUIRE(equality_only_type.equality_comparable());
    REQUIRE_FALSE(equality_only_type.hashable());
    EqualityOnly equality_only {1};
    REQUIRE(equality_only_type.equals(&equality_only, &equality_only));
    REQUIRE_FALSE(equality_only_type.hash_value(&equality_only));

    Type& no_equality_type = registry.register_type<NoEquality>();
    REQUIRE_FALSE(no_equality_type.equality_comparable());
    REQUIRE_FALSE(no_equality_type.hashable());
    NoEquality no_equality {1};
    REQUIRE_FALSE(no_equality_type.equals(&no_equality, &no_equality));
    REQUIRE_FALSE(no_equality_type.hash_value(&no_equality));

    Type& vector_type = registry.register_type<std::vector<NoEquality>>();
    REQUIRE_FALSE(vector_type.equality_comparable());
    REQUIRE_FALSE(vector_type.hashable());
}

TEST_CASE("Reflection types expose registered tags", "[refl][type][tag]") {
    Registry& registry = Registry::instance();
    Type& tagged_type = registry.register_type<TaggedType>();

    constexpr TypeTagId component_tag {"Component"};
    constexpr TypeTagId resource_tag {"Resource"};
    REQUIRE_FALSE(tagged_type.has_tag(component_tag));

    registry.add_generated_tag<TaggedType>("Component");
    registry.add_generated_tag<TaggedType>("Resource");
    registry.add_generated_tag<TaggedType>("Component");

    REQUIRE(tagged_type.has_tag(component_tag));
    REQUIRE(tagged_type.has_tag(resource_tag));
    REQUIRE(tagged_type.tags().size() == 2);
    const auto component_tag_name = registry.tag_name(component_tag);
    REQUIRE(component_tag_name.has_value());
    REQUIRE(*component_tag_name == "Component");

    const auto component_types = registry.types_with_tag(component_tag);
    REQUIRE(
        std::ranges::find(component_types, type_id<TaggedType>()) !=
        component_types.end()
    );
}

TEST_CASE("Generated reflection tags preserve values", "[refl][type][tag]") {
    Registry& registry = Registry::instance();
    register_generated_reflection();

    auto& reflected_type = registry.get_type(type_id<ReflectedTaggedType>());
    REQUIRE(reflected_type.has_structured_name());
    REQUIRE(reflected_type.namespace_path().size() == 2);
    CHECK(reflected_type.namespace_path()[0] == "ets");
    CHECK(reflected_type.namespace_path()[1] == "refl_test");
    CHECK(reflected_type.local_name() == "ReflectedTaggedType");
    constexpr TypeTagId plugin_tag {"Example"};
    constexpr TypeTagId plugin_name_tag {"Example.name"};
    constexpr TypeTagId plugin_phase_tag {"Example.phase"};

    const auto plugin = reflected_type.annotation("Example");
    REQUIRE(plugin);
    CHECK(plugin->name() == "Example");
    REQUIRE(plugin->value("name"));
    CHECK(*plugin->value("name") == "rendering");
    REQUIRE(plugin->value("phase"));
    CHECK(*plugin->value("phase") == "runtime");
    CHECK_FALSE(plugin->value("missing"));

    // The flattened tag API remains available as a compatibility index.
    REQUIRE(reflected_type.has_tag(plugin_tag));
    REQUIRE_FALSE(reflected_type.tag_value(plugin_tag));
    REQUIRE(reflected_type.has_tag(plugin_name_tag));
    REQUIRE(reflected_type.tag_value(plugin_name_tag));
    CHECK(*reflected_type.tag_value(plugin_name_tag) == "rendering");
    REQUIRE(reflected_type.has_tag(plugin_phase_tag));
    REQUIRE(reflected_type.tag_value(plugin_phase_tag));
    CHECK(*reflected_type.tag_value(plugin_phase_tag) == "runtime");

    auto& reflected_enum = registry.get_type(type_id<ReflectedTaggedEnum>());
    REQUIRE(reflected_enum.has_structured_name());
    REQUIRE(reflected_enum.namespace_path().size() == 2);
    CHECK(reflected_enum.namespace_path()[0] == "ets");
    CHECK(reflected_enum.namespace_path()[1] == "refl_test");
    CHECK(reflected_enum.local_name() == "ReflectedTaggedEnum");
    constexpr TypeTagId category_tag {"Category"};
    constexpr TypeTagId category_name_tag {"Category.name"};
    const auto category = reflected_enum.annotation("Category");
    REQUIRE(category);
    REQUIRE(category->value("name"));
    CHECK(*category->value("name") == "example");
    REQUIRE(reflected_enum.has_tag(category_tag));
    REQUIRE_FALSE(reflected_enum.tag_value(category_tag));
    REQUIRE(reflected_enum.has_tag(category_name_tag));
    REQUIRE(reflected_enum.tag_value(category_name_tag));
    CHECK(*reflected_enum.tag_value(category_name_tag) == "example");

    const auto plugin_types = registry.types_with_annotation("Example");
    CHECK(
        std::ranges::find(plugin_types, type_id<ReflectedTaggedType>()) !=
        plugin_types.end()
    );
}
