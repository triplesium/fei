#include "runtime_inspection/registry.hpp"

#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

using namespace ets;
using namespace ets::runtime_inspection;

namespace {

struct TestRequest {};
struct TestResponse {};

struct TestProvider {
    using Request = TestRequest;
    using Response = TestResponse;

    static constexpr std::string_view id {"test.inspect"};
    static constexpr std::string_view label {"Test inspection"};
    static constexpr std::string_view description {"Inspect a test value."};
    static constexpr std::string_view schema {"test.inspect.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        R"({"type":"object"})"
    };
    static constexpr std::string_view response_schema_json {
        R"({"type":"object"})"
    };

    Result<Response, InspectionError>
    inspect(const World&, const Request&) const {
        return Response {};
    }
};

static_assert(InspectionProvider<TestProvider>);

InspectionHandler echo_handler() {
    return [](const World&, std::string_view payload) {
        return Result<std::string, InspectionError>(std::string(payload));
    };
}

} // namespace

TEST_CASE(
    "Inspection registry dispatches providers by ID and schema",
    "[runtime-inspection][registry]"
) {
    InspectionRegistry registry;
    REQUIRE(registry.add<TestProvider>(echo_handler()));
    REQUIRE(registry.contains(TestProvider::id));
    REQUIRE(registry.descriptors().size() == 1);
    CHECK(registry.descriptors().front().label == TestProvider::label);
    CHECK(
        registry.descriptors().front().description == TestProvider::description
    );
    CHECK(registry.descriptors().front().read_only);
    CHECK(registry.descriptors().front().cost == InspectionCost::Low);
    CHECK(
        registry.descriptors().front().request_schema_json ==
        TestProvider::request_schema_json
    );

    World world;
    auto response = registry.dispatch(
        world,
        InspectionInvocation {
            .provider = TestProvider::id,
            .schema = TestProvider::schema,
            .payload_json = R"({"value":7})",
        }
    );
    REQUIRE(response);
    CHECK(*response == R"({"value":7})");
}

TEST_CASE(
    "Inspection registry rejects unknown providers and schemas",
    "[runtime-inspection][registry]"
) {
    InspectionRegistry registry;
    REQUIRE(registry.add<TestProvider>(echo_handler()));
    World world;

    auto unknown = registry.dispatch(
        world,
        InspectionInvocation {
            .provider = "missing.inspect",
            .schema = "missing.inspect.v1",
            .payload_json = "null",
        }
    );
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().kind == InspectionErrorKind::Unsupported);

    auto wrong_schema = registry.dispatch(
        world,
        InspectionInvocation {
            .provider = TestProvider::id,
            .schema = "test.inspect.v2",
            .payload_json = "null",
        }
    );
    REQUIRE_FALSE(wrong_schema);
    CHECK(wrong_schema.error().kind == InspectionErrorKind::Unsupported);
}

TEST_CASE(
    "Inspection registry rejects duplicates and freezes registration",
    "[runtime-inspection][registry]"
) {
    InspectionRegistry registry;
    REQUIRE(registry.add<TestProvider>(echo_handler()));

    auto duplicate = registry.add<TestProvider>(echo_handler());
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().kind == InspectionErrorKind::Conflict);

    registry.freeze();
    CHECK(registry.frozen());
    auto frozen = registry.add(
        InspectionDescriptor {
            .id = "other.inspect",
            .label = "Other inspection",
            .schema = "other.inspect.v1",
        },
        echo_handler()
    );
    REQUIRE_FALSE(frozen);
    CHECK(frozen.error().kind == InspectionErrorKind::Conflict);
}

TEST_CASE(
    "Inspection registry converts provider exceptions to internal errors",
    "[runtime-inspection][registry]"
) {
    InspectionRegistry registry;
    REQUIRE(registry.add<TestProvider>([](const World&, std::string_view) {
        throw std::runtime_error("provider failed");
        return Result<std::string, InspectionError>(std::string {});
    }));
    World world;

    auto response = registry.dispatch(
        world,
        InspectionInvocation {
            .provider = TestProvider::id,
            .schema = TestProvider::schema,
            .payload_json = "null",
        }
    );
    REQUIRE_FALSE(response);
    CHECK(response.error().kind == InspectionErrorKind::Internal);
}
