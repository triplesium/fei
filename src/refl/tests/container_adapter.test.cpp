#include "refl/container_adapter.hpp"

#include "base/optional.hpp"
#include "base/result.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace fei;

namespace {

struct PropertyContainerFixture {
    std::vector<std::string> names;
    std::array<int, 2> weights {};
    Optional<float> ratio;
    std::unordered_map<std::string, int> scores;
    std::map<int, std::string> labels;
    std::set<std::string> tags;
    std::unordered_set<int> flags;
    std::pair<int, std::string> pair;
    std::tuple<int, float, std::string> tuple;
};

struct UniquePtrIntLess {
    bool operator()(
        const std::unique_ptr<int>& lhs,
        const std::unique_ptr<int>& rhs
    ) const {
        return *lhs < *rhs;
    }
};

struct ConstructibleButNotAssignable {
    int value {0};

    ConstructibleButNotAssignable() = default;
    explicit ConstructibleButNotAssignable(int value) : value(value) {}
    ConstructibleButNotAssignable(const ConstructibleButNotAssignable&) =
        default;
    ConstructibleButNotAssignable(ConstructibleButNotAssignable&&) noexcept =
        default;
    ConstructibleButNotAssignable&
    operator=(const ConstructibleButNotAssignable&) = delete;
    ConstructibleButNotAssignable&
    operator=(ConstructibleButNotAssignable&&) = delete;
};

bool contains_exactly(
    const std::vector<TypeId>& values,
    std::initializer_list<TypeId> expected
) {
    return values == std::vector<TypeId>(expected);
}

TypeId generic_id(std::string_view name) {
    return TypeId(name);
}

template<class T>
ContainerAdapter& require_registered_adapter_from_property(
    std::string_view generic_name,
    std::initializer_list<TypeId> argument_type_ids
) {
    Registry& registry = Registry::instance();

    auto container_result = registry.try_get_container_adapter<T>();
    REQUIRE(container_result);

    auto generic_result = registry.try_get_generic_type<T>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->generic_type_id == generic_id(generic_name));
    REQUIRE(
        contains_exactly(generic_result->argument_type_ids, argument_type_ids)
    );

    return *container_result;
}

} // namespace

TEST_CASE(
    "Container adapters expose consistent semantic kinds",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    auto require_kind = [&]<class T>(ContainerKind expected) {
        registry.register_type<T>();
        auto result = registry.try_get_container_adapter<T>();
        REQUIRE(result);

        ContainerAdapter& container = *result;
        REQUIRE(container.kind() == expected);
        const bool is_associative =
            expected == ContainerKind::Map || expected == ContainerKind::Set;
        REQUIRE((container.associative() != nullptr) == is_associative);

        if (auto* associative = container.associative()) {
            REQUIRE(
                associative->has_mapped_value() ==
                (expected == ContainerKind::Map)
            );
        }
    };

    require_kind.operator()<std::vector<int>>(ContainerKind::Sequence);
    require_kind.operator()<std::array<int, 2>>(ContainerKind::Sequence);
    require_kind.operator()<Optional<int>>(ContainerKind::Optional);
    require_kind.operator()<std::pair<int, float>>(ContainerKind::Product);
    require_kind.operator()<std::tuple<int, float>>(ContainerKind::Product);
    require_kind.operator()<std::map<int, float>>(ContainerKind::Map);
    require_kind.operator()<std::unordered_map<int, float>>(ContainerKind::Map);
    require_kind.operator()<std::set<int>>(ContainerKind::Set);
    require_kind.operator()<std::unordered_set<int>>(ContainerKind::Set);
}

TEST_CASE(
    "Registry automatically registers vector container adapters",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    registry.register_type<std::vector<int>>();
    auto container_result =
        registry.try_get_container_adapter<std::vector<int>>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.container_type() == type_id<std::vector<int>>());
    REQUIRE(container.element_type() == type_id<int>());
    REQUIRE(container.kind() == ContainerKind::Sequence);
    REQUIRE_FALSE(container.fixed_size());

    std::vector<int> values {1, 2};
    auto size = container.size(Ref(values));
    REQUIRE(size);
    REQUIRE(*size == 2);

    const std::vector<int>& const_values = values;
    int next = 3;
    REQUIRE(container.append(Ref(values), Ref(next)));
    REQUIRE(values == std::vector<int> {1, 2, 3});

    int replacement = 10;
    REQUIRE(container.assign(Ref(values), 0, Ref(replacement)));
    REQUIRE(values == std::vector<int> {10, 2, 3});

    std::vector<int> visited;
    REQUIRE(container.for_each(
        Ref(values),
        [&](Ref element, std::size_t index) -> Status<ContainerError> {
            REQUIRE(index == visited.size());
            visited.push_back(element.get_const<int>());
            return {};
        }
    ));
    REQUIRE(visited == std::vector<int> {10, 2, 3});

    auto out_of_range = container.assign(Ref(values), 99, Ref(next));
    REQUIRE_FALSE(out_of_range);
    REQUIRE(out_of_range.error().kind == ContainerError::Kind::OutOfRange);

    REQUIRE_FALSE(container.append(Ref(const_values), Ref(next)));

    auto generic_result = registry.try_get_generic_type<std::vector<int>>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->specialized_type_id == type_id<std::vector<int>>());
    REQUIRE(generic_result->generic_type_id == generic_id("std::vector"));
    REQUIRE(generic_result->generic_name == "std::vector");
    REQUIRE(
        contains_exactly(generic_result->argument_type_ids, {type_id<int>()})
    );
}

TEST_CASE(
    "Vector container adapters support move-only element types",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestVector = std::vector<std::unique_ptr<int>>;
    registry.register_type<TestVector>();
    auto container_result = registry.try_get_container_adapter<TestVector>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    TestVector values;
    auto value = std::make_unique<int>(42);
    int* raw_value = value.get();

    REQUIRE(container.append(Ref(values), Ref(value)));
    REQUIRE(value == nullptr);
    REQUIRE(values.size() == 1);
    REQUIRE(values.front().get() == raw_value);
    REQUIRE(*values.front() == 42);

    const auto const_value = std::make_unique<int>(7);
    auto const_append = container.append(Ref(values), Ref(const_value));
    REQUIRE_FALSE(const_append);
    REQUIRE(const_append.error().kind == ContainerError::Kind::InvalidElement);
}

TEST_CASE(
    "Registry automatically registers fixed array container adapters",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestArray = std::array<float, 3>;
    registry.register_type<TestArray>();
    auto container_result = registry.try_get_container_adapter<TestArray>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.kind() == ContainerKind::Sequence);
    REQUIRE(container.fixed_size());

    TestArray values {1.0F, 2.0F, 3.0F};
    auto size = container.size(Ref(values));
    REQUIRE(size);
    REQUIRE(*size == 3);

    float replacement = 4.0F;
    REQUIRE(container.assign(Ref(values), 1, Ref(replacement)));
    REQUIRE(values[1] == 4.0F);

    std::vector<float> visited;
    REQUIRE(container.for_each(
        Ref(values),
        [&](Ref element, std::size_t) -> Status<ContainerError> {
            visited.push_back(element.get_const<float>());
            return {};
        }
    ));
    REQUIRE(visited == std::vector<float> {1.0F, 4.0F, 3.0F});

    auto generic_result = registry.try_get_generic_type<TestArray>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->generic_type_id == generic_id("std::array"));
    REQUIRE(
        contains_exactly(generic_result->argument_type_ids, {type_id<float>()})
    );
    REQUIRE(generic_result->arguments.size() == 2);
    REQUIRE(
        generic_result->arguments[0] == GenericArgument::type(type_id<float>())
    );
    REQUIRE(generic_result->arguments[1] == GenericArgument::unsigned_value(3));
}

TEST_CASE(
    "Fixed array container adapters support zero-sized arrays",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestArray = std::array<int, 0>;
    registry.register_type<TestArray>();
    auto container_result = registry.try_get_container_adapter<TestArray>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.kind() == ContainerKind::Sequence);
    REQUIRE(container.fixed_size());
    REQUIRE(container.element_type() == type_id<int>());

    TestArray values {};
    auto size = container.size(Ref(values));
    REQUIRE(size);
    REQUIRE(*size == 0);

    std::size_t visited = 0;
    REQUIRE(container.for_each(
        Ref(values),
        [&](Ref, std::size_t) -> Status<ContainerError> {
            ++visited;
            return {};
        }
    ));
    REQUIRE(visited == 0);

    auto generic_result = registry.try_get_generic_type<TestArray>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->arguments.size() == 2);
    REQUIRE(generic_result->arguments[1] == GenericArgument::unsigned_value(0));
}

TEST_CASE(
    "Container adapter assignment requires assignable elements",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestArray = std::array<ConstructibleButNotAssignable, 1>;
    registry.register_type<TestArray>();
    auto array_container_result =
        registry.try_get_container_adapter<TestArray>();
    REQUIRE(array_container_result);

    ContainerAdapter& array_container = *array_container_result;
    TestArray values {ConstructibleButNotAssignable {1}};

    REQUIRE(array_container.assign(Ref(values), 0, Ref(values[0])));
    REQUIRE(values[0].value == 1);

    ConstructibleButNotAssignable replacement {2};
    auto array_assignment =
        array_container.assign(Ref(values), 0, Ref(replacement));
    REQUIRE_FALSE(array_assignment);
    REQUIRE(
        array_assignment.error().kind ==
        ContainerError::Kind::UnsupportedOperation
    );
    REQUIRE(values[0].value == 1);

    using TestPair = std::pair<ConstructibleButNotAssignable, int>;
    registry.register_type<TestPair>();
    auto pair_container_result = registry.try_get_container_adapter<TestPair>();
    REQUIRE(pair_container_result);

    ContainerAdapter& pair_container = *pair_container_result;
    TestPair pair {ConstructibleButNotAssignable {3}, 4};
    auto pair_assignment =
        pair_container.assign(Ref(pair), 0, Ref(replacement));
    REQUIRE_FALSE(pair_assignment);
    REQUIRE(
        pair_assignment.error().kind ==
        ContainerError::Kind::UnsupportedOperation
    );
    REQUIRE(pair.first.value == 3);
}

TEST_CASE(
    "Registry automatically registers optional container adapters",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestOptional = Optional<std::string>;
    registry.register_type<TestOptional>();
    auto container_result = registry.try_get_container_adapter<TestOptional>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.kind() == ContainerKind::Optional);
    REQUIRE_FALSE(container.fixed_size());
    REQUIRE(container.element_type() == type_id<std::string>());

    TestOptional value;
    auto empty_size = container.size(Ref(value));
    REQUIRE(empty_size);
    REQUIRE(*empty_size == 0);
    std::string text {"hello"};
    REQUIRE(container.append(Ref(value), Ref(text)));
    REQUIRE(value.has_value());
    REQUIRE(*value == "hello");

    auto self_append = container.append(Ref(value), Ref(*value));
    REQUIRE_FALSE(self_append);
    REQUIRE(self_append.error().kind == ContainerError::Kind::OutOfRange);
    REQUIRE(*value == "hello");

    std::string replacement {"changed"};
    REQUIRE(container.assign(Ref(value), 0, Ref(replacement)));
    REQUIRE(*value == "changed");

    std::size_t visited = 0;
    REQUIRE(container.for_each(
        Ref(value),
        [&](Ref element, std::size_t index) -> Status<ContainerError> {
            REQUIRE(index == 0);
            REQUIRE(element.get_const<std::string>() == "changed");
            ++visited;
            return {};
        }
    ));
    REQUIRE(visited == 1);

    REQUIRE(container.clear(Ref(value)));
    REQUIRE_FALSE(value.has_value());

    std::string appended_after_clear {"again"};
    REQUIRE(container.append(Ref(value), Ref(appended_after_clear)));
    REQUIRE(*value == "again");

    auto generic_result = registry.try_get_generic_type<TestOptional>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->generic_type_id == generic_id("fei::Optional"));
    REQUIRE(contains_exactly(
        generic_result->argument_type_ids,
        {type_id<std::string>()}
    ));
}

TEST_CASE(
    "Registry automatically registers unordered_map container adapters",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestMap = std::unordered_map<std::string, int>;
    registry.register_type<TestMap>();
    auto container_result = registry.try_get_container_adapter<TestMap>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.kind() == ContainerKind::Map);
    REQUIRE(container.element_type() == type_id<TestMap::value_type>());

    auto* map_container = container.associative();
    REQUIRE(map_container != nullptr);
    REQUIRE(map_container->has_mapped_value());
    REQUIRE(map_container->key_type() == type_id<std::string>());
    REQUIRE(map_container->mapped_type() == type_id<int>());

    TestMap values {{"one", 1}};

    std::string two {"two"};
    int two_value = 2;
    REQUIRE(map_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(two), .value = Ref(two_value)}
    ));
    REQUIRE(values.at("two") == 2);

    two_value = 22;
    REQUIRE(map_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(two), .value = Ref(two_value)}
    ));
    REQUIRE(values.at("two") == 22);

    int visited_sum = 0;
    std::size_t visited_count = 0;
    REQUIRE(map_container->for_each_entry(
        Ref(values),
        [&](AssociativeElementRef entry,
            std::size_t) -> Status<ContainerError> {
            REQUIRE(entry.key.is_const());
            visited_sum += entry.value.get_const<int>();
            ++visited_count;
            return {};
        }
    ));
    REQUIRE(visited_count == 2);
    REQUIRE(visited_sum == 23);

    auto generic_result = registry.try_get_generic_type<TestMap>();
    REQUIRE(generic_result);
    REQUIRE(
        generic_result->generic_type_id == generic_id("std::unordered_map")
    );
    REQUIRE(contains_exactly(
        generic_result->argument_type_ids,
        {type_id<std::string>(), type_id<int>()}
    ));
}

TEST_CASE(
    "Registry automatically registers ordered map container adapters",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestMap = std::map<std::string, int>;
    registry.register_type<TestMap>();
    auto container_result = registry.try_get_container_adapter<TestMap>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.kind() == ContainerKind::Map);
    REQUIRE(container.element_type() == type_id<TestMap::value_type>());

    auto* map_container = container.associative();
    REQUIRE(map_container != nullptr);
    REQUIRE(map_container->has_mapped_value());
    REQUIRE(map_container->key_type() == type_id<std::string>());
    REQUIRE(map_container->mapped_type() == type_id<int>());

    TestMap values {{"one", 1}};
    std::string two {"two"};
    int two_value = 2;
    REQUIRE(map_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(two), .value = Ref(two_value)}
    ));
    REQUIRE(values.at("two") == 2);

    std::vector<std::string> visited_keys;
    REQUIRE(map_container->for_each_entry(
        Ref(values),
        [&](AssociativeElementRef entry,
            std::size_t) -> Status<ContainerError> {
            visited_keys.push_back(entry.key.get_const<std::string>());
            return {};
        }
    ));
    REQUIRE(visited_keys == std::vector<std::string> {"one", "two"});

    auto generic_result = registry.try_get_generic_type<TestMap>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->generic_type_id == generic_id("std::map"));
    REQUIRE(contains_exactly(
        generic_result->argument_type_ids,
        {type_id<std::string>(), type_id<int>()}
    ));
}

TEST_CASE(
    "Registry automatically registers set container adapters",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestSet = std::set<std::string>;
    registry.register_type<TestSet>();
    auto container_result = registry.try_get_container_adapter<TestSet>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.kind() == ContainerKind::Set);
    REQUIRE(container.element_type() == type_id<std::string>());

    auto* set_container = container.associative();
    REQUIRE(set_container != nullptr);
    REQUIRE_FALSE(set_container->has_mapped_value());
    REQUIRE(set_container->key_type() == type_id<std::string>());
    REQUIRE_FALSE(set_container->mapped_type());

    TestSet values {"one"};
    std::string two {"two"};
    REQUIRE(set_container
                ->insert(Ref(values), AssociativeElementRef {.key = Ref(two)}));
    REQUIRE(values.contains("two"));

    std::size_t visited_count = 0;
    REQUIRE(set_container->for_each_entry(
        Ref(values),
        [&](AssociativeElementRef entry,
            std::size_t) -> Status<ContainerError> {
            REQUIRE(entry.key.is_const());
            REQUIRE_FALSE(entry.value);
            ++visited_count;
            return {};
        }
    ));
    REQUIRE(visited_count == 2);

    auto generic_result = registry.try_get_generic_type<TestSet>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->generic_type_id == generic_id("std::set"));
    REQUIRE(contains_exactly(
        generic_result->argument_type_ids,
        {type_id<std::string>()}
    ));
}

TEST_CASE(
    "Registry automatically registers unordered_set container adapters",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestSet = std::unordered_set<int>;
    registry.register_type<TestSet>();
    auto container_result = registry.try_get_container_adapter<TestSet>();
    REQUIRE(container_result);

    ContainerAdapter& container = *container_result;
    REQUIRE(container.kind() == ContainerKind::Set);

    auto* set_container = container.associative();
    REQUIRE(set_container != nullptr);
    REQUIRE_FALSE(set_container->has_mapped_value());

    TestSet values;
    int value = 7;
    REQUIRE(set_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(value)}
    ));
    REQUIRE(values.contains(7));

    auto generic_result = registry.try_get_generic_type<TestSet>();
    REQUIRE(generic_result);
    REQUIRE(
        generic_result->generic_type_id == generic_id("std::unordered_set")
    );
    REQUIRE(
        contains_exactly(generic_result->argument_type_ids, {type_id<int>()})
    );
}

TEST_CASE(
    "Unordered map value_type is reflected as a const-key pair container",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestMap = std::unordered_map<std::string, int>;
    using Entry = TestMap::value_type;
    registry.register_type<TestMap>();

    auto entry_container_result = registry.try_get_container_adapter<Entry>();
    REQUIRE(entry_container_result);

    ContainerAdapter& entry_container = *entry_container_result;
    REQUIRE(entry_container.kind() == ContainerKind::Product);
    REQUIRE(entry_container.fixed_size());
    REQUIRE_FALSE(entry_container.element_type());
    REQUIRE(*entry_container.element_type(0) == type_id<std::string>());
    REQUIRE(*entry_container.element_type(1) == type_id<int>());

    Entry entry {"one", 1};
    std::size_t visited = 0;
    REQUIRE(entry_container.for_each(
        Ref(entry),
        [&](Ref element, std::size_t index) -> Status<ContainerError> {
            if (index == 0) {
                REQUIRE(element.is_const());
                REQUIRE(element.type_id() == type_id<std::string>());
                REQUIRE(element.get_const<std::string>() == "one");
            } else {
                REQUIRE(index == 1);
                REQUIRE_FALSE(element.is_const());
                REQUIRE(element.get<int>() == 1);
            }
            ++visited;
            return {};
        }
    ));
    REQUIRE(visited == 2);

    std::string replacement_key {"two"};
    auto key_assign =
        entry_container.assign(Ref(entry), 0, Ref(replacement_key));
    REQUIRE_FALSE(key_assign);
    REQUIRE(key_assign.error().kind == ContainerError::Kind::InvalidElement);

    int replacement_value = 10;
    REQUIRE(entry_container.assign(Ref(entry), 1, Ref(replacement_value)));
    REQUIRE(entry.second == 10);
}

TEST_CASE(
    "Registry records generic metadata for non-container templates",
    "[refl][generic]"
) {
    Registry& registry = Registry::instance();

    using TestResult = Result<int, std::string>;
    registry.register_type<TestResult>();

    auto generic_result = registry.try_get_generic_type<TestResult>();
    REQUIRE(generic_result);
    REQUIRE(generic_result->specialized_type_id == type_id<TestResult>());
    REQUIRE(generic_result->generic_type_id == generic_id("fei::Result"));
    REQUIRE(generic_result->generic_name == "fei::Result");
    REQUIRE(contains_exactly(
        generic_result->argument_type_ids,
        {type_id<int>(), type_id<std::string>()}
    ));

    auto container_result = registry.try_get_container_adapter<TestResult>();
    REQUIRE_FALSE(container_result);
    REQUIRE(
        container_result.error().kind ==
        RegistryError::Kind::ContainerAdapterNotFound
    );

    using TestPair = std::pair<int, std::string>;
    registry.register_type<TestPair>();

    auto pair_result = registry.try_get_generic_type<TestPair>();
    REQUIRE(pair_result);
    REQUIRE(pair_result->specialized_type_id == type_id<TestPair>());
    REQUIRE(pair_result->generic_type_id == generic_id("std::pair"));
    REQUIRE(pair_result->generic_name == "std::pair");
    REQUIRE(contains_exactly(
        pair_result->argument_type_ids,
        {type_id<int>(), type_id<std::string>()}
    ));
    auto pair_container_result = registry.try_get_container_adapter<TestPair>();
    REQUIRE(pair_container_result);
    ContainerAdapter& pair_container = *pair_container_result;
    REQUIRE(pair_container.fixed_size());
    REQUIRE(pair_container.kind() == ContainerKind::Product);
    TestPair empty_pair {};
    auto pair_size = pair_container.size(Ref(empty_pair));
    REQUIRE(pair_size);
    REQUIRE(*pair_size == 2);
    REQUIRE_FALSE(pair_container.element_type());
    REQUIRE(*pair_container.element_type(0) == type_id<int>());
    REQUIRE(*pair_container.element_type(1) == type_id<std::string>());

    TestPair pair {7, "seven"};
    std::size_t pair_visited = 0;
    REQUIRE(pair_container.for_each(
        Ref(pair),
        [&](Ref element, std::size_t index) -> Status<ContainerError> {
            if (index == 0) {
                REQUIRE(element.get<int>() == 7);
            } else {
                REQUIRE(element.get<std::string>() == "seven");
            }
            ++pair_visited;
            return {};
        }
    ));
    REQUIRE(pair_visited == 2);
    int pair_replacement = 9;
    REQUIRE(pair_container.assign(Ref(pair), 0, Ref(pair_replacement)));
    REQUIRE(pair.first == 9);
    REQUIRE_FALSE(pair_container.assign(Ref(pair), 2, Ref(pair_replacement)));

    using TestTuple = std::tuple<int, float, std::string>;
    registry.register_type<TestTuple>();

    auto tuple_result = registry.try_get_generic_type<TestTuple>();
    REQUIRE(tuple_result);
    REQUIRE(tuple_result->specialized_type_id == type_id<TestTuple>());
    REQUIRE(tuple_result->generic_type_id == generic_id("std::tuple"));
    REQUIRE(tuple_result->generic_name == "std::tuple");
    REQUIRE(contains_exactly(
        tuple_result->argument_type_ids,
        {type_id<int>(), type_id<float>(), type_id<std::string>()}
    ));
    auto tuple_container_result =
        registry.try_get_container_adapter<TestTuple>();
    REQUIRE(tuple_container_result);
    ContainerAdapter& tuple_container = *tuple_container_result;
    REQUIRE(tuple_container.fixed_size());
    REQUIRE(tuple_container.kind() == ContainerKind::Product);
    REQUIRE_FALSE(tuple_container.element_type());
    REQUIRE(*tuple_container.element_type(0) == type_id<int>());
    REQUIRE(*tuple_container.element_type(1) == type_id<float>());
    REQUIRE(*tuple_container.element_type(2) == type_id<std::string>());

    TestTuple tuple {3, 1.5F, "tuple"};
    std::string tuple_replacement {"assigned"};
    REQUIRE(tuple_container.assign(Ref(tuple), 2, Ref(tuple_replacement)));
    REQUIRE(std::get<2>(tuple) == "assigned");

    using EmptyTuple = std::tuple<>;
    registry.register_type<EmptyTuple>();

    auto empty_tuple_result = registry.try_get_generic_type<EmptyTuple>();
    REQUIRE(empty_tuple_result);
    REQUIRE(empty_tuple_result->specialized_type_id == type_id<EmptyTuple>());
    REQUIRE(empty_tuple_result->generic_type_id == generic_id("std::tuple"));
    REQUIRE(empty_tuple_result->argument_type_ids.empty());

    auto empty_tuple_container_result =
        registry.try_get_container_adapter<EmptyTuple>();
    REQUIRE(empty_tuple_container_result);
    ContainerAdapter& empty_tuple_container = *empty_tuple_container_result;
    REQUIRE(empty_tuple_container.fixed_size());
    REQUIRE(empty_tuple_container.kind() == ContainerKind::Product);
    REQUIRE_FALSE(empty_tuple_container.element_type());

    EmptyTuple empty_tuple;
    auto empty_tuple_size = empty_tuple_container.size(Ref(empty_tuple));
    REQUIRE(empty_tuple_size);
    REQUIRE(*empty_tuple_size == 0);
    REQUIRE_FALSE(empty_tuple_container.element_type(0));
    std::size_t empty_visited = 0;
    REQUIRE(empty_tuple_container.for_each(
        Ref(empty_tuple),
        [&](Ref, std::size_t) -> Status<ContainerError> {
            ++empty_visited;
            return {};
        }
    ));
    REQUIRE(empty_visited == 0);
}

TEST_CASE(
    "Unordered map container adapters support move-only mapped types",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestMap = std::unordered_map<int, std::unique_ptr<int>>;
    registry.register_type<TestMap>();
    auto container_result = registry.try_get_container_adapter<TestMap>();
    REQUIRE(container_result);

    auto* map_container = container_result->associative();
    REQUIRE(map_container != nullptr);

    TestMap values;
    int key = 4;
    auto value = std::make_unique<int>(44);
    int* raw_value = value.get();

    REQUIRE(map_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(key), .value = Ref(value)}
    ));
    REQUIRE(value == nullptr);
    REQUIRE(values.at(4).get() == raw_value);
    REQUIRE(*values.at(4) == 44);

    auto replacement = std::make_unique<int>(55);
    REQUIRE(map_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(key), .value = Ref(replacement)}
    ));
    REQUIRE(replacement == nullptr);
    REQUIRE(*values.at(4) == 55);

    const auto const_value = std::make_unique<int>(66);
    auto const_insert = map_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(key), .value = Ref(const_value)}
    );
    REQUIRE_FALSE(const_insert);
    REQUIRE(const_insert.error().kind == ContainerError::Kind::InvalidElement);
}

TEST_CASE(
    "Ordered map container adapters support move-only mapped types",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestMap = std::map<int, std::unique_ptr<int>>;
    registry.register_type<TestMap>();
    auto container_result = registry.try_get_container_adapter<TestMap>();
    REQUIRE(container_result);

    auto* map_container = container_result->associative();
    REQUIRE(map_container != nullptr);

    TestMap values;
    int key = 4;
    auto value = std::make_unique<int>(44);
    int* raw_value = value.get();

    REQUIRE(map_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(key), .value = Ref(value)}
    ));
    REQUIRE(value == nullptr);
    REQUIRE(values.at(4).get() == raw_value);
    REQUIRE(*values.at(4) == 44);
}

TEST_CASE(
    "Set container adapters support move-only key types",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    using TestSet = std::set<std::unique_ptr<int>, UniquePtrIntLess>;
    registry.register_type<TestSet>();
    auto container_result = registry.try_get_container_adapter<TestSet>();
    REQUIRE(container_result);

    auto* set_container = container_result->associative();
    REQUIRE(set_container != nullptr);
    REQUIRE_FALSE(set_container->has_mapped_value());

    TestSet values;
    auto key = std::make_unique<int>(44);
    int* raw_key = key.get();

    REQUIRE(set_container
                ->insert(Ref(values), AssociativeElementRef {.key = Ref(key)}));
    REQUIRE(key == nullptr);
    REQUIRE(values.size() == 1);
    REQUIRE(values.begin()->get() == raw_key);
    REQUIRE(*(*values.begin()) == 44);

    const auto const_key = std::make_unique<int>(55);
    auto const_insert = set_container->insert(
        Ref(values),
        AssociativeElementRef {.key = Ref(const_key)}
    );
    REQUIRE_FALSE(const_insert);
    REQUIRE(const_insert.error().kind == ContainerError::Kind::InvalidElement);
}

TEST_CASE(
    "Class property registration registers container member types",
    "[refl][container]"
) {
    Registry& registry = Registry::instance();

    registry.register_cls<PropertyContainerFixture>()
        .add_property("names", &PropertyContainerFixture::names)
        .add_property("weights", &PropertyContainerFixture::weights)
        .add_property("ratio", &PropertyContainerFixture::ratio)
        .add_property("scores", &PropertyContainerFixture::scores)
        .add_property("labels", &PropertyContainerFixture::labels)
        .add_property("tags", &PropertyContainerFixture::tags)
        .add_property("flags", &PropertyContainerFixture::flags)
        .add_property("pair", &PropertyContainerFixture::pair)
        .add_property("tuple", &PropertyContainerFixture::tuple);

    auto& names =
        require_registered_adapter_from_property<std::vector<std::string>>(
            "std::vector",
            {type_id<std::string>()}
        );
    REQUIRE(names.kind() == ContainerKind::Sequence);
    REQUIRE(names.element_type() == type_id<std::string>());

    auto& weights =
        require_registered_adapter_from_property<std::array<int, 2>>(
            "std::array",
            {type_id<int>()}
        );
    REQUIRE(weights.kind() == ContainerKind::Sequence);
    REQUIRE(weights.fixed_size());
    REQUIRE(weights.element_type() == type_id<int>());

    auto& ratio = require_registered_adapter_from_property<Optional<float>>(
        "fei::Optional",
        {type_id<float>()}
    );
    REQUIRE(ratio.kind() == ContainerKind::Optional);
    REQUIRE_FALSE(ratio.fixed_size());
    REQUIRE(ratio.element_type() == type_id<float>());

    auto& scores = require_registered_adapter_from_property<
        std::unordered_map<std::string, int>>(
        "std::unordered_map",
        {type_id<std::string>(), type_id<int>()}
    );
    REQUIRE(scores.kind() == ContainerKind::Map);
    REQUIRE(scores.associative()->has_mapped_value());
    REQUIRE(scores.associative()->mapped_type() == type_id<int>());

    auto& labels =
        require_registered_adapter_from_property<std::map<int, std::string>>(
            "std::map",
            {type_id<int>(), type_id<std::string>()}
        );
    REQUIRE(labels.kind() == ContainerKind::Map);
    REQUIRE(labels.associative()->has_mapped_value());
    REQUIRE(labels.associative()->mapped_type() == type_id<std::string>());

    auto& tags =
        require_registered_adapter_from_property<std::set<std::string>>(
            "std::set",
            {type_id<std::string>()}
        );
    REQUIRE(tags.kind() == ContainerKind::Set);
    REQUIRE_FALSE(tags.associative()->has_mapped_value());
    REQUIRE(tags.associative()->key_type() == type_id<std::string>());

    auto& flags =
        require_registered_adapter_from_property<std::unordered_set<int>>(
            "std::unordered_set",
            {type_id<int>()}
        );
    REQUIRE(flags.kind() == ContainerKind::Set);
    REQUIRE_FALSE(flags.associative()->has_mapped_value());
    REQUIRE(flags.associative()->key_type() == type_id<int>());

    auto& pair =
        require_registered_adapter_from_property<std::pair<int, std::string>>(
            "std::pair",
            {type_id<int>(), type_id<std::string>()}
        );
    REQUIRE(pair.kind() == ContainerKind::Product);
    REQUIRE(pair.fixed_size());

    auto& tuple = require_registered_adapter_from_property<
        std::tuple<int, float, std::string>>(
        "std::tuple",
        {type_id<int>(), type_id<float>(), type_id<std::string>()}
    );
    REQUIRE(tuple.kind() == ContainerKind::Product);
    REQUIRE(tuple.fixed_size());
}
