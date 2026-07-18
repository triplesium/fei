#include "rendering/mesh/mesh_uniform.hpp"

#include "ecs/world.hpp"
#include "rendering/components.hpp"
#include "test_graphics_device.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

TEST_CASE(
    "mesh uniforms share one aligned buffer upload",
    "[rendering][mesh][uniform]"
) {
    World world;
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(world.resource<GraphicsDevice>());
    device.uniform_buffer_alignment = 128;
    world.add_resource(RenderQueue {});
    world.add_resource(MeshUniforms {});

    std::vector<Entity> entities;
    for (uint32 index = 0; index < 3; ++index) {
        const auto entity = world.entity();
        world.add_component(entity, Mesh3d {});
        world.add_component(
            entity,
            Transform3d {.position = {static_cast<float>(index), 0.0f, 0.0f}}
        );
        entities.push_back(entity);
    }

    world.run_system_once(prepare_mesh_uniforms);

    const auto& mesh_uniforms = world.resource<MeshUniforms>();
    REQUIRE(mesh_uniforms.resource_layout);
    REQUIRE(mesh_uniforms.uniform_buffer);
    REQUIRE(mesh_uniforms.resource_set);
    REQUIRE(mesh_uniforms.stride == 128);
    REQUIRE(mesh_uniforms.capacity == 4);
    REQUIRE(mesh_uniforms.entries.size() == 3);
    REQUIRE(mesh_uniforms.upload_data.size() == 3 * 128);
    REQUIRE(world.resource<RenderQueue>().pending_buffer_writes() == 1);

    REQUIRE(device.resource_layout_descriptions.size() == 1);
    REQUIRE(device.resource_layout_descriptions[0].elements.size() == 1);
    CHECK(device.resource_layout_descriptions[0].elements[0].options.is_set(
        ResourceLayoutElementOptions::DynamicBinding
    ));

    REQUIRE(device.buffer_descriptions.size() == 1);
    CHECK(device.buffer_descriptions[0].size == 4 * 128);
    CHECK(device.buffer_descriptions[0].usages == BufferUsages::Uniform);

    std::vector<uint32> offsets;
    for (const auto entity : entities) {
        offsets.push_back(mesh_uniforms.entries.at(entity).dynamic_offset);
    }
    std::ranges::sort(offsets);
    CHECK(offsets == std::vector<uint32> {0, 128, 256});

    REQUIRE(device.resource_set_descriptions.size() == 1);
    REQUIRE(device.resource_set_descriptions[0].resources.size() == 1);
    const auto range = std::dynamic_pointer_cast<const BufferRange>(
        device.resource_set_descriptions[0].resources[0]
    );
    REQUIRE(range);
    CHECK(range->buffer() == mesh_uniforms.uniform_buffer);
    CHECK(range->offset() == 0);
    CHECK(range->size() == sizeof(MeshUniform));
}
