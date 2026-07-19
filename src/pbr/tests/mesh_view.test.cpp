#include "pbr/mesh_view.hpp"

#include "../../rendering/tests/test_graphics_device.hpp"
#include "ecs/commands.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/lut.hpp"
#include "rendering/render_queue.hpp"

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
    "prepare_mesh_view_resource_set covers camera and shadow views",
    "[pbr][mesh_view]"
) {
    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(world.resource<GraphicsDevice>());

    world.add_resource(MeshViewLayout {});
    world.add_resource(RenderQueue {});
    world.add_resource(
        GpuLUTs {
            .brdf_lut = test_image(device, TextureType::Texture2D),
        }
    );
    world.add_resource(MeshViewResourceSet {});

    const auto camera = world.entity();
    world.add_component(camera, Camera3d {});
    world.add_component(
        camera,
        GpuEnvironmentMap {
            .environment_cubemap = test_image(device, TextureType::Texture2D),
            .irradiance_cubemap = test_image(device, TextureType::Texture2D),
            .radiance_cubemap = test_image(device, TextureType::Texture2D),
        }
    );
    world.add_component(camera, EnvironmentMapLight {});
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

    const auto shadow_view = world.entity();
    world.add_component(
        shadow_view,
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
    REQUIRE(device.sampler_descriptions.size() == 2);
    REQUIRE(device.resource_layout_descriptions.size() == 1);
    const auto& mesh_view_elements =
        device.resource_layout_descriptions.front().elements;
    REQUIRE(mesh_view_elements.size() == 7);
    CHECK(mesh_view_elements[4].name == "brdf_lut");
    CHECK(mesh_view_elements[4].kind == ResourceKind::TextureReadOnly);
    CHECK(mesh_view_elements[5].name == "brdf_sampler");
    CHECK(mesh_view_elements[5].kind == ResourceKind::Sampler);
    CHECK(mesh_view_elements[6].name == "EnvironmentMap");
    CHECK(mesh_view_elements[6].kind == ResourceKind::UniformBuffer);

    world.run_system_once(prepare_mesh_view_resource_set);
    world.resource<CommandsQueue>().execute(world);

    REQUIRE(world.has_component<MeshViewResourceSet>(camera));
    REQUIRE(world.has_component<MeshViewResourceSet>(shadow_view));
    REQUIRE(device.resource_set_descriptions.size() == 2);
    const auto& mesh_view_layout = world.resource<MeshViewLayout>();
    for (const auto& resource_set : device.resource_set_descriptions) {
        REQUIRE(resource_set.resources.size() == 7);
        CHECK(
            resource_set.resources[5].get() ==
            mesh_view_layout.brdf_sampler.get()
        );
    }
    REQUIRE(world.get_component<MeshViewResourceSet>(camera)
                .environment_uniform_buffer);
    REQUIRE(world.get_component<MeshViewResourceSet>(shadow_view)
                .environment_uniform_buffer);
    REQUIRE(world.resource<RenderQueue>().pending_buffer_writes() == 2);

    world.run_system_once(prepare_mesh_view_resource_set);
    world.resource<CommandsQueue>().execute(world);

    CHECK(device.sampler_descriptions.size() == 2);
    CHECK(device.resource_set_descriptions.size() == 2);
    CHECK(world.resource<RenderQueue>().pending_buffer_writes() == 4);
    CHECK(world.resource<MeshViewResourceSet>().resource_set);
}
