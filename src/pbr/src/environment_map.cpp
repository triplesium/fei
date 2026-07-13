#include "pbr/environment_map.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/plugin.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader_cache.hpp"

#include <bit>

namespace fei {

struct FilteringConstants {
    float roughness;
};

void generated_equirect_env_map_to_env_map(
    Query<Entity, const GeneratedEquirectEnvironmentMap>::Filter<
        Without<EnvironmentMap>> query,
    ResRW<Assets<Image>> images,
    Commands commands
) {
    for (auto [entity, gen_env_map] : query) {
        auto environment_cubemap = Image::create_empty(
            1024,
            1024,
            6,
            PixelFormat::Rgba32Float,
            {TextureUsage::Sampled,
             TextureUsage::Cubemap,
             TextureUsage::Storage},
            TextureType::Texture2D
        );
        auto environment_cubemap_handle =
            images->add(std::move(environment_cubemap));
        auto irradiance_image = Image::create_empty(
            32,
            32,
            6,
            PixelFormat::Rgba32Float,
            {TextureUsage::Sampled,
             TextureUsage::Cubemap,
             TextureUsage::Storage},
            TextureType::Texture2D
        );
        auto irradiance_image_handle = images->add(std::move(irradiance_image));

        uint32 base_size = 1024;
        auto radiance_image = Image::create_empty(
            base_size,
            base_size,
            6,
            PixelFormat::Rgba32Float,
            {TextureUsage::Sampled,
             TextureUsage::Cubemap,
             TextureUsage::Storage,
             TextureUsage::GenerateMipmaps},
            TextureType::Texture2D
        );
        radiance_image->texture_description().mip_level =
            static_cast<uint32>(std::floor(std::log2(base_size))) + 1;
        auto radiance_image_handle = images->add(std::move(radiance_image));
        commands.entity(entity).add(
            EnvironmentMap {
                .environment_cubemap = environment_cubemap_handle,
                .irradiance_cubemap = irradiance_image_handle,
                .radiance_cubemap = radiance_image_handle,
            }
        );
    }
}

void insert_gpu_env_map(
    Query<Entity, const GeneratedEquirectEnvironmentMap, const EnvironmentMap>::
        Filter<Without<GpuEnvironmentMap>> query,
    ResRO<RenderAssets<GpuImage>> gpu_images,
    Commands commands
) {
    for (auto [entity, gen_env_map, env_map] : query) {
        auto environment_gpu_image =
            gpu_images->get(env_map.environment_cubemap.id());
        auto irradiance_gpu_image =
            gpu_images->get(env_map.irradiance_cubemap.id());
        auto radiance_gpu_image =
            gpu_images->get(env_map.radiance_cubemap.id());
        if (!irradiance_gpu_image || !radiance_gpu_image ||
            !environment_gpu_image) {
            continue;
        }
        commands.entity(entity).add(
            GpuEnvironmentMap {
                .environment_cubemap = *environment_gpu_image,
                .irradiance_cubemap = *irradiance_gpu_image,
                .radiance_cubemap = *radiance_gpu_image,
            }
        );
    }
}

void convert_equirect_to_cubemap(
    Query<Entity, const GeneratedEquirectEnvironmentMap, GpuEnvironmentMap>
        query,
    ResRO<GraphicsDevice> device,
    ResRO<Assets<Image>> images,
    ResRO<RenderAssets<GpuImage>> gpu_images,
    ResRW<EquirectToCubemap> equirect_to_cubemap
) {
    for (auto [entity, gen_env_map, gpu_env_map] : query) {
        auto equirect_gpu_image =
            gpu_images->get(gen_env_map.equirect_image.id());
        if (!equirect_gpu_image) {
            continue;
        }
        auto environment_cubemap = equirect_to_cubemap->get_or_create_cubemap(
            *device,
            *images,
            gen_env_map.equirect_image
        );
        if (!environment_cubemap) {
            continue;
        }
        gpu_env_map.environment_cubemap = *environment_cubemap;
    }
}

// TODO: Pipeline caching
void generate_env_maps(
    Query<Entity, const GpuEnvironmentMap>::Filter<
        Without<EnvironmentMapGeneratedTag>> query,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
) {
    for (auto [entity, gpu_env_map] : query) {

        auto cubemap_sampler =
            device->create_sampler(SamplerDescription::Linear);

        {
            auto compute_shader = shader_cache->get_or_compile(
                AssetPath("shader://pbr/cubemap2irradiance.slang"),
                ShaderStages::Compute,
                {}
            );
            auto layout = device->create_resource_layout(
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
                            .name = "cubemap_sampler",
                            .kind = ResourceKind::Sampler,
                            .stages = {ShaderStages::Compute},
                        },
                        {
                            .binding = 2,
                            .name = "output_texture",
                            .kind = ResourceKind::TextureReadWrite,
                            .stages = {ShaderStages::Compute},
                        }
                    },
                }
            );
            auto pipeline = device->create_compute_pipeline(
                ComputePipelineDescription {
                    .shader = compute_shader,
                    .resource_layouts = {layout},
                }
            );
            auto resource_set = device->create_resource_set(
                ResourceSetDescription {
                    .layout = layout,
                    .resources =
                        {gpu_env_map.environment_cubemap.texture(),
                         cubemap_sampler,
                         gpu_env_map.irradiance_cubemap.texture()},
                    .name = "environment.irradiance",
                }
            );
            auto command_buffer = device->create_command_buffer();
            command_buffer->begin();
            command_buffer->set_compute_pipeline(pipeline);
            command_buffer->set_resource_set(0, resource_set);
            command_buffer->dispatch(
                gpu_env_map.irradiance_cubemap.texture()->width() / 8,
                gpu_env_map.irradiance_cubemap.texture()->height() / 8,
                6
            );
            command_buffer->end();
            device->submit_commands(command_buffer);
        }

        {
            auto compute_shader = shader_cache->get_or_compile(
                AssetPath("shader://pbr/cubemap2radiance.slang"),
                ShaderStages::Compute,
                {}
            );
            auto uniform_buffer = device->create_buffer(
                BufferDescription {
                    .size = sizeof(FilteringConstants),
                    .usages = BufferUsages::Uniform,
                }
            );
            auto layout = device->create_resource_layout(
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
                            .name = "cubemap_sampler",
                            .kind = ResourceKind::Sampler,
                            .stages = {ShaderStages::Compute},
                        },
                        {
                            .binding = 2,
                            .name = "output_texture",
                            .kind = ResourceKind::TextureReadWrite,
                            .stages = {ShaderStages::Compute},
                        },
                        {
                            .binding = 3,
                            .name = "Constants",
                            .kind = ResourceKind::UniformBuffer,
                            .stages = {ShaderStages::Compute},
                        }
                    },
                }
            );
            auto pipeline = device->create_compute_pipeline(
                ComputePipelineDescription {
                    .shader = compute_shader,
                    .resource_layouts = {layout},
                }
            );
            auto command_buffer = device->create_command_buffer();
            command_buffer->begin();
            command_buffer->generate_mipmaps(
                gpu_env_map.radiance_cubemap.texture()
            );
            command_buffer->set_compute_pipeline(pipeline);
            uint32 map_width = gpu_env_map.radiance_cubemap.texture()->width();
            uint32 num_mips = 32u - std::countl_zero(map_width);
            for (uint32 level = 0, size = map_width; level < num_mips;
                 level++, size /= 2) {
                auto texture_view = device->create_texture_view(
                    TextureViewDescription {
                        .target = gpu_env_map.radiance_cubemap.texture(),
                        .base_mip_level = level,
                        .mip_levels = 1,
                        .base_array_layer = 0,
                        .array_layers = 1,
                    }
                );
                FilteringConstants uniform {
                    .roughness = static_cast<float>(level) /
                                 static_cast<float>(num_mips - 1),
                };
                command_buffer->update_buffer(
                    uniform_buffer,
                    &uniform,
                    sizeof(FilteringConstants)
                );
                auto resource_set = device->create_resource_set(
                    ResourceSetDescription {
                        .layout = layout,
                        .resources =
                            {gpu_env_map.environment_cubemap.texture(),
                             cubemap_sampler,
                             texture_view,
                             uniform_buffer},
                        .name = "environment.radiance",
                    }
                );
                command_buffer->set_resource_set(0, resource_set);
                command_buffer->dispatch(
                    gpu_env_map.radiance_cubemap.texture()->width() / 8,
                    gpu_env_map.radiance_cubemap.texture()->height() / 8,
                    6
                );
            }
            command_buffer->end();
            device->submit_commands(command_buffer);
        }

        commands.entity(entity).add(EnvironmentMapGeneratedTag {});
    }
}

void EnvironmentMapPlugin::setup(App& app) {
    app.add_systems(Update, generated_equirect_env_map_to_env_map)
        .add_systems(
            RenderUpdate,
            chain(
                insert_gpu_env_map,
                convert_equirect_to_cubemap,
                generate_env_maps
            ) | in_set<RenderingSystems::PrepareResources>() |
                in_set<PbrSystems::PrepareEnvironmentMaps>()
        );
}

} // namespace fei
