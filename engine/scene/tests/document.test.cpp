#include "scene/document.hpp"

#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

using namespace ets;

namespace {

struct DocumentTestComponent {
    int value {0};
};

void register_document_test_component() {
    Registry::instance().register_cls<DocumentTestComponent>().add_property(
        "value",
        &DocumentTestComponent::value
    );
    Registry::instance().add_generated_tag<DocumentTestComponent>("Component");
}

constexpr std::string_view scene_source = R"(
format: entisium.scene
version: 1
entities:
  - id: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    components:
      DocumentTestComponent:
        value: 17
      MissingPluginComponent:
        label: "preserved"
        boolean_looking_string: "true"
  - id: "556942a5-6ddd-4887-a425-a0953013024c"
    parent: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    components:
      DocumentTestComponent:
        value: 29
)";

} // namespace

TEST_CASE("Scene documents round trip YAML", "[scene][document]") {
    auto document = parse_scene_document(scene_source);
    REQUIRE(document);
    REQUIRE(document->entities.size() == 2);
    REQUIRE(document->entities[1].parent);
    CHECK(*document->entities[1].parent == document->entities[0].id);

    auto encoded = write_scene_document(*document);
    REQUIRE(encoded);
    auto decoded = parse_scene_document(*encoded);
    REQUIRE(decoded);
    REQUIRE(decoded->entities.size() == 2);
    CHECK(decoded->entities[0].components[1].type == "MissingPluginComponent");
    const auto* properties =
        decoded->entities[0].components[1].properties.try_object();
    REQUIRE(properties);
    REQUIRE(properties->size() == 2);
    CHECK(*properties->front().value.try_string() == "preserved");
    CHECK(*properties->back().value.try_string() == "true");
}

TEST_CASE(
    "Scene documents instantiate and capture reflected components",
    "[scene][document][reflection]"
) {
    register_document_test_component();
    auto document = parse_scene_document(scene_source);
    REQUIRE(document);

    World world;
    auto instantiated = instantiate_scene_document(*document, world);
    REQUIRE(instantiated);
    REQUIRE(instantiated->warnings.size() == 1);

    const auto root = instantiated->bindings.entity(document->entities[0].id);
    const auto child = instantiated->bindings.entity(document->entities[1].id);
    REQUIRE(root);
    REQUIRE(child);
    CHECK(world.get_component<DocumentTestComponent>(*root).value == 17);
    CHECK(world.get_component<DocumentTestComponent>(*child).value == 29);
    const auto runtime_parent = world.parent(*child);
    REQUIRE(runtime_parent);
    CHECK(*runtime_parent == *root);

    auto captured = capture_scene_document(
        world,
        instantiated->bindings,
        nullptr,
        &*document
    );
    REQUIRE(captured);
    REQUIRE(captured->entities.size() == 2);
    CHECK(captured->entities[0].id == document->entities[0].id);
    REQUIRE(captured->entities[0].components.size() == 2);
    CHECK(captured->entities[0].components[1].type == "MissingPluginComponent");
}

TEST_CASE("Scene documents validate entity references", "[scene][document]") {
    const auto invalid = parse_scene_document(R"(
format: entisium.scene
version: 1
entities:
  - id: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    parent: "556942a5-6ddd-4887-a425-a0953013024c"
    components: {}
)");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().path == "$.entities[0].parent");
}

TEST_CASE("Scene documents reject hierarchy cycles", "[scene][document]") {
    const auto invalid = parse_scene_document(R"(
format: entisium.scene
version: 1
entities:
  - id: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    parent: "556942a5-6ddd-4887-a425-a0953013024c"
    components: {}
  - id: "556942a5-6ddd-4887-a425-a0953013024c"
    parent: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    components: {}
)");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().message.contains("cycle"));
}
