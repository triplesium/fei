#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "ecs/system_params.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/opengl/plugin.hpp"
#include "pbr/plugin.hpp"
#include "rendering/plugin.hpp"
#include "rendering/shader.hpp"
#include "window/window.hpp"

#include <print>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

using namespace fei;

struct Global {
    std::shared_ptr<Texture> cubemap;
};

void equirect_to_cubemap(
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shaders,
    Res<Assets<Image>> images,
    Res<Global> global
) {
    auto equirect_image_handle =
        asset_server->load<Image>("the_sky_is_on_fire_4k.hdr");
    auto& equirect_image = images->get(equirect_image_handle).value();
    auto equirect_texture =
        device->create_texture(equirect_image.texture_description());
    device->update_texture(
        equirect_texture,
        equirect_image.data(),
        0,
        0,
        0,
        equirect_image.width(),
        equirect_image.height(),
        equirect_image.channels(),
        0,
        0
    );
    std::println(
        "Equirectangular texture: {}x{}",
        equirect_texture->width(),
        equirect_texture->height()
    );
    auto cubemap_texture = device->create_texture(TextureDescription {
        .width = 512,
        .height = 512,
        .depth = 6,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba32Float,
        .texture_usage =
            {
                TextureUsage::Sampled,
                TextureUsage::Storage,
                TextureUsage::Cubemap,
            },
        .texture_type = TextureType::Texture2D,
    });
    auto shader_handle =
        asset_server->load<Shader>("embeded://equirect2cube.comp");
    auto& shader = shaders->get(shader_handle).value();
    auto compute_shader = device->create_shader_module(shader.description());
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription {
            .elements =
                {{
                     .binding = 0,
                     .name = "input_texture",
                     .kind = ResourceKind::TextureReadOnly,
                     .stages = {ShaderStages::Compute},
                 },
                 {
                     .binding = 1,
                     .name = "output_texture",
                     .kind = ResourceKind::TextureReadWrite,
                     .stages = {ShaderStages::Compute},
                 }},
        });
    auto compute_pipeline =
        device->create_compute_pipeline(ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {resource_layout},
        });
    auto resource_set = device->create_resource_set(ResourceSetDescription {
        .layout = resource_layout,
        .resources = {equirect_texture, cubemap_texture},
    });
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(compute_pipeline);
    command_buffer->set_resource_set(0, resource_set);
    command_buffer->dispatch(
        equirect_texture->width() / 32,
        equirect_texture->height() / 32,
        6
    );
    command_buffer->end();
    device->submit_commands(command_buffer);

    auto mapped = device->map(cubemap_texture, MapMode::Read);
    auto data = mapped.data();
    std::println("{}", data.size_bytes());

    stbi_flip_vertically_on_write(1);
    for (uint32_t face = 0; face < 6; ++face) {
        float* float_data = reinterpret_cast<float*>(
            data.subspan(face * 512 * 512 * 16, 512 * 512 * 16).data()
        );
        int ret = stbi_write_hdr(
            std::format(FEI_ASSETS_PATH "/../temp/cubemap_face_{}.hdr", face)
                .c_str(),
            512,
            512,
            4,
            float_data
        );
        if (ret == 0) {
            fei::fatal("Failed to write cubemap face {}", face);
        }
    }
    device->unmap(cubemap_texture);

    global->cubemap = cubemap_texture;
}

void cubemap_to_irradiance_map(
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shaders,
    Res<Assets<Image>> images,
    Res<Global> global
) {
    auto irradiance_texture = device->create_texture(TextureDescription {
        .width = 32,
        .height = 32,
        .depth = 6,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba32Float,
        .texture_usage =
            {
                TextureUsage::Sampled,
                TextureUsage::Storage,
                TextureUsage::Cubemap,
            },
        .texture_type = TextureType::Texture2D,
    });
    auto shader_handle = asset_server->load<Shader>("cubemap2irradiance.comp");
    auto& shader = shaders->get(shader_handle).value();
    auto compute_shader = device->create_shader_module(shader.description());
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription {
            .elements =
                {{
                     .binding = 0,
                     .name = "cubemap",
                     .kind = ResourceKind::TextureReadOnly,
                     .stages = {ShaderStages::Compute},
                 },
                 {
                     .binding = 1,
                     .name = "output_texture",
                     .kind = ResourceKind::TextureReadWrite,
                     .stages = {ShaderStages::Compute},
                 }},
        });
    auto compute_pipeline =
        device->create_compute_pipeline(ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {resource_layout},
        });
    auto resource_set = device->create_resource_set(ResourceSetDescription {
        .layout = resource_layout,
        .resources = {global->cubemap, irradiance_texture},
    });
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(compute_pipeline);
    command_buffer->set_resource_set(0, resource_set);
    command_buffer->dispatch(
        irradiance_texture->width() / 32,
        irradiance_texture->height() / 32,
        6
    );
    command_buffer->end();
    device->submit_commands(command_buffer);

    auto mapped = device->map(irradiance_texture, MapMode::Read);
    auto data = mapped.data();
    std::println("{}", data.size_bytes());

    stbi_flip_vertically_on_write(1);
    for (uint32_t face = 0; face < 6; ++face) {
        float* float_data = reinterpret_cast<float*>(
            data.subspan(face * 32 * 32 * 16, 32 * 32 * 16).data()
        );
        int ret = stbi_write_hdr(
            std::format(FEI_ASSETS_PATH "/../temp/irradiance_face_{}.hdr", face)
                .c_str(),
            32,
            32,
            4,
            float_data
        );
        if (ret == 0) {
            fei::fatal("Failed to write irradiance face {}", face);
        }
    }
    device->unmap(irradiance_texture);
}

int main() {
    App()
        .add_plugins(
            AssetsPlugin {},
            ImagePlugin {},
            WindowPlugin {},
            OpenGLPlugin {},
            RenderingPlugin {},
            PbrPlugin {}
        )
        .add_resource(Global {})
        .add_systems(
            StartUp,
            chain(equirect_to_cubemap, cubemap_to_irradiance_map)
        )
        .add_systems(
            Update,
            [](Res<AppStates> states) {
                states->should_stop = true;
            }
        )
        .run();

    return 0;
}
