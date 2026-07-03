#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::rendering_test;

TEST_CASE(
    "RenderingPlugin skips present when MainSwapchain is absent",
    "[rendering][plugin]"
) {
    App app;
    app.add_plugin<AssetsPlugin>()
        .add_resource_as<GraphicsDevice>(FakeGraphicsDevice {})
        .add_plugin<RenderingPlugin>();

    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(app.resource<GraphicsDevice>());

    app.world().sort_systems();
    app.run_schedule(RenderLast);

    REQUIRE(device.present_calls == 0);
}
