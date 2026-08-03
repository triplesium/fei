#include "reflection_metadata.hpp"

#include "devtools/json.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "refl/utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::devtools::reflection;

namespace reflection_metadata_test {

struct MetadataFixture {
    int value {0};

    explicit MetadataFixture(int initial) : value(initial) {}

    int read() const { return value; }
    int read(int offset) const { return value + offset; }
};

enum class MetadataMode {
    Idle = -1,
    Active = 2,
};

namespace left {
struct Ambiguous {};
} // namespace left

namespace right {
struct Ambiguous {};
} // namespace right

void register_fixture_types() {
    static bool registered = false;
    if (registered) {
        return;
    }

    register_generated_reflection();
    auto& registry = Registry::instance();
    registry.register_cls<MetadataFixture>()
        .add_property("value", &MetadataFixture::value)
        .add_method(
            "read",
            static_cast<int (MetadataFixture::*)() const>(
                &MetadataFixture::read
            )
        )
        .add_method(
            "read",
            static_cast<int (MetadataFixture::*)(int) const>(
                &MetadataFixture::read
            )
        )
        .add_constructor<MetadataFixture, int>();
    registry.register_enum<MetadataMode>()
        .add_enumerator("Idle", static_cast<std::int64_t>(MetadataMode::Idle))
        .add_enumerator(
            "Active",
            static_cast<std::int64_t>(MetadataMode::Active)
        );
    registry.register_type<std::vector<MetadataFixture>>();
    registry.register_type<left::Ambiguous>();
    registry.register_type<right::Ambiguous>();
    registered = true;
}

const TypeSummary*
find_summary(const SearchResponse& response, const std::string& name) {
    for (const auto& summary : response.matches) {
        if (summary.name == name) {
            return &summary;
        }
    }
    return nullptr;
}

bool has_facet(const TypeSummary& summary, const std::string& facet) {
    for (const auto& value : summary.facets) {
        if (value == facet) {
            return true;
        }
    }
    return false;
}

} // namespace reflection_metadata_test

TEST_CASE(
    "Reflection search returns deterministic type summaries",
    "[devtools][reflection]"
) {
    using namespace reflection_metadata_test;
    register_fixture_types();

    auto decoded = devtools::decode_json<SearchRequest>(
        R"({"pattern":"metadatafixture","limit":20})"
    );
    REQUIRE(decoded);

    auto response = search_types(*decoded);
    REQUIRE(response);
    REQUIRE_FALSE(response->truncated);

    const auto fixture_name = std::string(type_name<MetadataFixture>());
    const auto* fixture = find_summary(*response, fixture_name);
    REQUIRE(fixture != nullptr);
    REQUIRE(has_facet(*fixture, "class"));
    REQUIRE(fixture->id == format_type_id(type_id<MetadataFixture>()));

    auto json = devtools::encode_json(Ref(*response));
    REQUIRE(json);
    REQUIRE(json->find("metadatafixture") == std::string::npos);
    REQUIRE(json->find(fixture_name) != std::string::npos);

    for (std::size_t index = 1; index < response->matches.size(); ++index) {
        REQUIRE(
            response->matches[index - 1].name < response->matches[index].name
        );
    }

    auto limited = search_types(SearchRequest {.pattern = {}, .limit = 1});
    REQUIRE(limited);
    REQUIRE(limited->matches.size() == 1);
    REQUIRE(limited->truncated);

    auto invalid = search_types(
        SearchRequest {
            .pattern = {},
            .limit = c_max_search_limit + 1,
        }
    );
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error().status == 400);
}

TEST_CASE(
    "Reflection describe exposes class members and operations",
    "[devtools][reflection]"
) {
    using namespace reflection_metadata_test;
    register_fixture_types();

    auto response = describe_type(
        DescribeRequest {.type = std::string(type_name<MetadataFixture>())}
    );
    REQUIRE(response);
    REQUIRE(response->summary.name == type_name<MetadataFixture>());
    REQUIRE(response->properties.size() == 1);
    REQUIRE(response->properties.front().name == "value");
    REQUIRE(response->properties.front().type.name == type_name<int>());
    REQUIRE(response->methods.size() == 2);
    REQUIRE(response->methods.front().name == "read");
    REQUIRE(response->methods.front().is_const);
    REQUIRE(response->constructors.size() == 1);
    REQUIRE(response->constructors.front().parameters.size() == 1);
    REQUIRE(
        response->constructors.front().parameters.front().type.type.name ==
        type_name<int>()
    );
    REQUIRE(response->operations.copy_constructible);
    REQUIRE(response->operations.destructible);

    auto by_id = describe_type(
        DescribeRequest {.type = format_type_id(type_id<MetadataFixture>())}
    );
    REQUIRE(by_id);
    REQUIRE(by_id->summary.name == type_name<MetadataFixture>());
}

TEST_CASE(
    "Reflection describe exposes enum and container metadata",
    "[devtools][reflection]"
) {
    using namespace reflection_metadata_test;
    register_fixture_types();

    auto enum_type = describe_type(
        DescribeRequest {.type = std::string(type_name<MetadataMode>())}
    );
    REQUIRE(enum_type);
    REQUIRE(has_facet(enum_type->summary, "enum"));
    REQUIRE(enum_type->enum_values.size() == 2);
    REQUIRE(enum_type->enum_values.front().name == "Active");
    REQUIRE(enum_type->enum_values.back().value == "-1");

    using FixtureVector = std::vector<MetadataFixture>;
    auto container = describe_type(
        DescribeRequest {.type = std::string(type_name<FixtureVector>())}
    );
    REQUIRE(container);
    REQUIRE(has_facet(container->summary, "generic"));
    REQUIRE(has_facet(container->summary, "container"));
    REQUIRE(container->generic.present);
    REQUIRE(container->container.present);
    REQUIRE(container->container.kind == "sequence");
    REQUIRE(
        container->container.element_type.name == type_name<MetadataFixture>()
    );
    REQUIRE_FALSE(container->container.fixed_size);
}

TEST_CASE(
    "Reflection describe reports invalid, missing, and ambiguous selectors",
    "[devtools][reflection]"
) {
    using namespace reflection_metadata_test;
    register_fixture_types();

    auto invalid = describe_type(DescribeRequest {.type = "0xnot-a-type"});
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error().status == 400);

    auto missing = describe_type(DescribeRequest {.type = "MissingType"});
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().status == 404);

    auto ambiguous = describe_type(DescribeRequest {.type = "Ambiguous"});
    REQUIRE_FALSE(ambiguous);
    REQUIRE(ambiguous.error().status == 409);
}
