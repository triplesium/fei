#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "ecs/system_params.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "pbr/plugin.hpp"
#include "rendering/plugin.hpp"
#include "rendering/shader.hpp"

#include <cstddef>
#include <format>
#include <print>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

using namespace fei;

struct Global {
    std::shared_ptr<Texture> cubemap;
};

std::shared_ptr<Texture> copy_to_staging_texture(
    ResRO<GraphicsDevice> device,
    const std::shared_ptr<Texture>& texture
) {
    auto usage = BitFlags<TextureUsage> {TextureUsage::Staging};
    if (texture->usage().is_set(TextureUsage::Cubemap)) {
        usage.set(TextureUsage::Cubemap);
    }

    auto staging_texture = device->create_texture(
        TextureDescription {
            .width = texture->width(),
            .height = texture->height(),
            .depth = texture->depth(),
            .mip_level = texture->mip_level(),
            .layer = texture->layer(),
            .texture_format = texture->format(),
            .texture_usage = usage,
            .texture_type = texture->type(),
            .sample_count = texture->sample_count(),
        }
    );

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->copy_texture(texture, staging_texture);
    command_buffer->end();
    device->submit_commands(command_buffer);

    return staging_texture;
}

void equirect_to_cubemap(
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shaders,
    ResRW<Assets<Image>> images,
    ResRW<Global> global
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
        equirect_image.depth(),
        0,
        0
    );
    std::println(
        "Equirectangular texture: {}x{}",
        equirect_texture->width(),
        equirect_texture->height()
    );
    auto cubemap_texture = device->create_texture(
        TextureDescription {
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
        }
    );
    auto shader_handle =
        asset_server->load<Shader>("shader://equirect2cube.comp");
    auto& shader = shaders->get(shader_handle).value();
    auto compute_shader = device->create_shader_module(shader.description());
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription {
            .elements = {
                {
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
                }
            },
        }
    );
    auto compute_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {resource_layout},
        }
    );
    auto resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resource_layout,
            .resources = {equirect_texture, cubemap_texture},
        }
    );
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

    auto staging_texture = copy_to_staging_texture(device, cubemap_texture);
    auto mapped = device->map(staging_texture, MapMode::Read);
    auto data = mapped.data();
    std::println("{}", data.size_bytes());

    stbi_flip_vertically_on_write(1);
    constexpr std::size_t cubemap_face_bytes = std::size_t {512} * 512 * 16;
    for (uint32_t face = 0; face < 6; ++face) {
        float* float_data = reinterpret_cast<float*>(
            data.subspan(face * cubemap_face_bytes, cubemap_face_bytes).data()
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
    device->unmap(staging_texture);

    global->cubemap = cubemap_texture;
}

void cubemap_to_irradiance_map(
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shaders,
    ResRO<Global> global
) {
    auto irradiance_texture = device->create_texture(
        TextureDescription {
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
        }
    );
    auto shader_handle =
        asset_server->load<Shader>("shader://cubemap2irradiance.comp");
    auto& shader = shaders->get(shader_handle).value();
    auto compute_shader = device->create_shader_module(shader.description());
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription {
            .elements = {
                {
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
                }
            },
        }
    );
    auto compute_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {resource_layout},
        }
    );
    auto resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resource_layout,
            .resources = {global->cubemap, irradiance_texture},
        }
    );
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

    auto staging_texture = copy_to_staging_texture(device, irradiance_texture);
    auto mapped = device->map(staging_texture, MapMode::Read);
    auto data = mapped.data();
    std::println("{}", data.size_bytes());

    stbi_flip_vertically_on_write(1);
    constexpr std::size_t irradiance_face_bytes = std::size_t {32} * 32 * 16;
    for (uint32_t face = 0; face < 6; ++face) {
        float* float_data = reinterpret_cast<float*>(
            data.subspan(face * irradiance_face_bytes, irradiance_face_bytes)
                .data()
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
    device->unmap(staging_texture);
}

int main() {
    App()
        .add_plugins(
            AssetsPlugin {},
            ImagePlugin {},
            OpenGLGlfwPlugin {},
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
            [](ResRW<AppStates> states) {
                states->should_stop = true;
            }
        )
        .run();

    return 0;
}
