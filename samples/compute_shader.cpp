#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "ecs/system_params.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/plugin.hpp"
#include "rendering/extract_resource.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader_cache.hpp"

#include <cstddef>
#include <filesystem>
#include <format>
#include <print>
#include <utility>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

using namespace fei;

struct Global {
    std::shared_ptr<Texture> cubemap;
    bool irradiance_complete {false};
};

struct ComputeShaderInput {
    Handle<Image> equirect_image;
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
    ResRW<ShaderCache> shader_cache,
    ResRO<ComputeShaderInput> input,
    ResRW<RenderAssets<GpuImage>> images,
    ResRW<Global> global
) {
    if (global->cubemap) {
        return;
    }
    auto equirect_image = images->get(input->equirect_image);
    if (!equirect_image) {
        return;
    }
    auto equirect_texture = equirect_image->texture();
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
    auto compute_shader = shader_cache->get_or_compile(
        AssetPath("shader://pbr/equirect2cube.slang"),
        ShaderStages::Compute,
        {}
    );
    auto sampler = device->create_sampler(SamplerDescription::Linear);
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            ShaderStages::Compute,
            {
                texture_read_only("input_texture"),
                fei::sampler("input_sampler"),
                texture_read_write("output_texture"),
                uniform_buffer("constants"),
            }
        )
    );
    auto compute_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {resource_layout},
        }
    );
    auto output_view = device->create_texture_view(
        TextureViewDescription {
            .target = cubemap_texture,
            .view_type = TextureViewType::Texture2DArray,
        }
    );
    auto constants_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(EquirectToCubemapUniform),
            .usages = BufferUsages::Uniform,
        }
    );
    const EquirectToCubemapUniform constants {
        .width = cubemap_texture->width(),
        .height = cubemap_texture->height(),
        .layers = cubemap_texture->depth(),
    };
    auto resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resource_layout,
            .resources =
                {equirect_texture, sampler, output_view, constants_buffer},
        }
    );
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer
        ->update_buffer(constants_buffer, &constants, sizeof(constants));
    command_buffer->set_compute_pipeline(compute_pipeline);
    command_buffer->set_resource_set(0, resource_set);
    command_buffer->dispatch(
        (cubemap_texture->width() + 15) / 16,
        (cubemap_texture->height() + 15) / 16,
        constants.layers
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
    ResRW<ShaderCache> shader_cache,
    ResRW<Global> global
) {
    if (!global->cubemap || global->irradiance_complete) {
        return;
    }
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
    auto compute_shader = shader_cache->get_or_compile(
        AssetPath("shader://pbr/cubemap2irradiance.slang"),
        ShaderStages::Compute,
        {}
    );
    auto sampler = device->create_sampler(SamplerDescription::Linear);
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            ShaderStages::Compute,
            {
                texture_read_only("cubemap"),
                fei::sampler("cubemap_sampler"),
                texture_read_write("output_texture"),
            }
        )
    );
    auto compute_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {resource_layout},
        }
    );
    auto output_view = device->create_texture_view(
        TextureViewDescription {
            .target = irradiance_texture,
            .view_type = TextureViewType::Texture2DArray,
        }
    );
    auto resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resource_layout,
            .resources = {global->cubemap, sampler, output_view},
        }
    );
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(compute_pipeline);
    command_buffer->set_resource_set(0, resource_set);
    command_buffer->dispatch(
        irradiance_texture->width() / 8,
        irradiance_texture->height() / 8,
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
    global->irradiance_complete = true;
}

int main() {
    std::filesystem::create_directories(FEI_ASSETS_PATH "/../temp");

    App app;
    app.add_plugins(
        AssetsPlugin {},
        ImagePlugin {},
        OpenGLGlfwPlugin {},
        RenderingPlugin {},
        PbrPlugin {}
    );

    auto equirect_image =
        app.resource<AssetServer>().load<Image>("the_sky_is_on_fire_4k.hdr");
    app.add_resource(
        ComputeShaderInput {
            .equirect_image = std::move(equirect_image),
        }
    );
    add_extract_resource<ComputeShaderInput>(app);
    app.sub_app<RenderApp>().add_resource(Global {}).add_systems(
        RenderUpdate,
        chain(equirect_to_cubemap, cubemap_to_irradiance_map) |
            in_set<RenderingSystems::PrepareResources>() | main_thread()
    );
    app.add_systems(
           Update,
           [](ResRW<AppStates> states) {
               states->should_stop = true;
           }
    ).run();

    return 0;
}
