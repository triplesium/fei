#include "rendering/view.hpp"

#include "ecs/world.hpp"
#include "test_graphics_device.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace ets;
using namespace ets::rendering_test;

TEST_CASE(
    "view uniforms share one aligned dynamic buffer",
    "[rendering][view][uniform]"
) {
    World world;
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(world.resource<GraphicsDevice>());
    device.uniform_buffer_alignment = 256;
    world.add_resource(RenderQueue {});
    world.add_resource(ViewUniforms {});

    const auto first = world.entity();
    const auto second = world.entity();
    world.add_component(first, PreparedView {});
    world.add_component(second, PreparedView {});

    world.run_system_once(upload_view_uniforms);

    const auto& uniforms = world.resource<ViewUniforms>().buffer;
    REQUIRE(uniforms.buffer());
    CHECK(uniforms.stride() == 512);
    CHECK(uniforms.size() == 2);
    CHECK(uniforms.capacity() == 2);
    CHECK(uniforms.buffer_revision() == 1);
    CHECK(world.resource<RenderQueue>().pending_buffer_writes() == 1);

    std::vector<uint32> offsets {
        world.get_component<PreparedView>(first).dynamic_offset,
        world.get_component<PreparedView>(second).dynamic_offset,
    };
    std::ranges::sort(offsets);
    CHECK(offsets == std::vector<uint32> {0, 512});

    const auto first_buffer = uniforms.buffer();
    world.run_system_once(upload_view_uniforms);
    CHECK(world.resource<ViewUniforms>().buffer.buffer() == first_buffer);
    CHECK(world.resource<ViewUniforms>().buffer.buffer_revision() == 1);

    const auto third = world.entity();
    world.add_component(third, PreparedView {});
    world.run_system_once(upload_view_uniforms);

    CHECK(world.resource<ViewUniforms>().buffer.capacity() == 4);
    CHECK(world.resource<ViewUniforms>().buffer.buffer() != first_buffer);
    CHECK(world.resource<ViewUniforms>().buffer.buffer_revision() == 2);
}
