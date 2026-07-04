#include "pbr/mesh_view.hpp"

#include "../../rendering/tests/test_graphics_device.hpp"
#include "ecs/commands.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/lut.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::rendering_test;

namespace {

TextureDescription test_texture_description(TextureType texture_type) {
    return TextureDescription {
        .width = 4,
        .height = 4,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = TextureUsage::Sampled,
        .texture_type = texture_type,
    };
}

GpuImage test_image(FakeGraphicsDevice& device, TextureType texture_type) {
    return GpuImage(
        device.create_texture(test_texture_description(texture_type))
    );
}

} // namespace

TEST_CASE(
    "prepare_mesh_view_resource_set reuses stable camera resource sets",
    "[pbr][mesh_view]"
) {
    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(world.resource<GraphicsDevice>());

    world.add_resource(MeshViewLayout {});
    world.add_resource(
        GpuLUTs {
            .brdf_lut = test_image(device, TextureType::Texture2D),
        }
    );
    world.add_resource(MeshViewResourceSet {});

    const auto env_entity = world.entity();
    world.add_component(
        env_entity,
        GpuEnvironmentMap {
            .environment_cubemap = test_image(device, TextureType::Texture2D),
            .irradiance_cubemap = test_image(device, TextureType::Texture2D),
            .radiance_cubemap = test_image(device, TextureType::Texture2D),
        }
    );

    const auto camera = world.entity();
    world.add_component(
        camera,
        ViewUniformBuffer {
            .buffer = device.create_buffer(
                BufferDescription {
                    .size = sizeof(ViewUniform),
                    .usages = BufferUsages::Uniform,
                }
            ),
        }
    );

    world.run_system_once(init_mesh_view_layout);
    REQUIRE(device.sampler_descriptions.size() == 1);

    world.run_system_once(prepare_mesh_view_resource_set);
    world.resource<CommandsQueue>().execute(world);

    REQUIRE(world.has_component<MeshViewResourceSet>(camera));
    REQUIRE(device.resource_set_descriptions.size() == 1);

    world.run_system_once(prepare_mesh_view_resource_set);
    world.resource<CommandsQueue>().execute(world);

    CHECK(device.sampler_descriptions.size() == 1);
    CHECK(device.resource_set_descriptions.size() == 1);
    CHECK(world.resource<MeshViewResourceSet>().resource_set);
}
