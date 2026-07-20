#include "ecs/world.hpp"
#include "pbr/passes/target.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::rendering_test;

TEST_CASE(
    "Deferred view targets persist, resize, minimize, and recover",
    "[pbr][deferred][targets]"
) {
    World world;
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    world.add_resource(
        Window {
            .glfw_window = nullptr,
            .width = 1280,
            .height = 720,
        }
    );
    world.add_resource(DeferredViewTargets {});

    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(world.resource<GraphicsDevice>());
    auto prepare = [&]() {
        world.run_system_once(prepare_deferred_view_targets);
    };

    prepare();
    auto& targets = world.resource<DeferredViewTargets>();
    REQUIRE(targets.valid());
    REQUIRE(targets.width == 1280);
    REQUIRE(targets.height == 720);
    REQUIRE(device.texture_descriptions.size() == 8);
    REQUIRE(targets.composite->format() == PixelFormat::Rgba16Float);
    auto first_composite = targets.composite;

    prepare();
    REQUIRE(targets.composite == first_composite);
    REQUIRE(device.texture_descriptions.size() == 8);

    world.resource<Window>().width = 1920;
    world.resource<Window>().height = 1080;
    prepare();
    REQUIRE(targets.valid());
    REQUIRE(targets.composite != first_composite);
    REQUIRE(device.texture_descriptions.size() == 16);

    world.resource<Window>().width = 0;
    world.resource<Window>().height = 0;
    prepare();
    REQUIRE_FALSE(targets.valid());
    REQUIRE(targets.width == 0);
    REQUIRE(targets.height == 0);

    world.resource<Window>().width = 800;
    world.resource<Window>().height = 600;
    prepare();
    REQUIRE(targets.valid());
    REQUIRE(targets.width == 800);
    REQUIRE(targets.height == 600);
    REQUIRE(device.texture_descriptions.size() == 24);
}
