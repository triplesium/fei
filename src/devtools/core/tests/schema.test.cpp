#include "devtools/schema.hpp"

#include "base/optional.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using namespace fei;
using namespace fei::devtools;

namespace {

enum class SchemaMode {
    Idle,
    Active,
};

struct SchemaNested {
    bool enabled {false};
};

struct SchemaFixture {
    SchemaMode mode {SchemaMode::Idle};
    SchemaNested nested;
    std::vector<int> values;
    Optional<std::string> label;
    std::unordered_map<std::string, int> scores;
};

void register_schema_types() {
    static bool registered = false;
    if (registered) {
        return;
    }

    auto& registry = Registry::instance();
    registry.register_enum<SchemaMode>()
        .add_enumerator("Idle", static_cast<std::int64_t>(SchemaMode::Idle))
        .add_enumerator(
            "Active",
            static_cast<std::int64_t>(SchemaMode::Active)
        );
    registry.register_cls<SchemaNested>().add_property(
        "enabled",
        &SchemaNested::enabled
    );
    registry.register_cls<SchemaFixture>()
        .add_property("mode", &SchemaFixture::mode)
        .add_property("nested", &SchemaFixture::nested)
        .add_property("values", &SchemaFixture::values)
        .add_property("label", &SchemaFixture::label)
        .add_property("scores", &SchemaFixture::scores);
    registered = true;
}

std::string reflected_name(TypeId id) {
    return Registry::instance().try_get_type(id)->name();
}

const nlohmann::json&
find_property(const nlohmann::json& type, const std::string& name) {
    for (const auto& property : type.at("properties")) {
        if (property.at("name") == name) {
            return property;
        }
    }
    FAIL("Missing schema property: " << name);
    return type;
}

} // namespace

TEST_CASE(
    "DevTools schema describes reflected object graphs",
    "[devtools][schema]"
) {
    register_schema_types();

    auto encoded = build_schema_json({type_id<SchemaFixture>()});
    REQUIRE(encoded);
    auto document = nlohmann::json::parse(*encoded);
    const auto& types = document.at("types");

    auto fixture_name = reflected_name(type_id<SchemaFixture>());
    auto mode_name = reflected_name(type_id<SchemaMode>());
    auto vector_name = reflected_name(type_id<std::vector<int>>());
    auto optional_name = reflected_name(type_id<Optional<std::string>>());
    auto map_name =
        reflected_name(type_id<std::unordered_map<std::string, int>>());

    REQUIRE(document.at("version") == 1);
    REQUIRE(document.at("roots") == nlohmann::json::array({fixture_name}));
    REQUIRE(types.at(fixture_name).at("kind") == "object");

    REQUIRE(
        find_property(types.at(fixture_name), "mode").at("type") == mode_name
    );
    REQUIRE(types.at(mode_name).at("kind") == "enum");
    REQUIRE(types.at(mode_name).at("values").size() == 2);

    REQUIRE(
        find_property(types.at(fixture_name), "values").at("type") ==
        vector_name
    );
    REQUIRE(types.at(vector_name).at("kind") == "sequence");
    REQUIRE(
        types.at(vector_name).at("element_type") ==
        reflected_name(type_id<int>())
    );

    REQUIRE(types.at(optional_name).at("kind") == "optional");
    REQUIRE(
        types.at(optional_name).at("element_type") ==
        reflected_name(type_id<std::string>())
    );

    REQUIRE(types.at(map_name).at("kind") == "map");
    REQUIRE(
        types.at(map_name).at("key_type") ==
        reflected_name(type_id<std::string>())
    );
    REQUIRE(
        types.at(map_name).at("mapped_type") == reflected_name(type_id<int>())
    );
}
