#include "pbr/environment_map.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "ecs/event.hpp"
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

#include <algorithm>
#include <cmath>

namespace fei {

struct FilteringConstants {
    float roughness;
};

EnvironmentMap
create_environment_map(ResRW<Assets<Image>> images, AssetId source) {
    auto irradiance_image = Image::create_empty(
        32,
        32,
        6,
        PixelFormat::Rgba32Float,
        {TextureUsage::Sampled, TextureUsage::Cubemap, TextureUsage::Storage},
        TextureType::Texture2D
    );

    constexpr uint32 base_size = 1024;
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

    return EnvironmentMap {
        .source_equirect_image = source,
        .irradiance_cubemap = images->add(std::move(irradiance_image)),
        .radiance_cubemap = images->add(std::move(radiance_image)),
    };
}

const EnvironmentMap& get_or_create_environment_map(
    AssetId source,
    ResRW<Assets<Image>> images,
    ResRW<EnvironmentMapCache> cache
) {
    auto [iter, inserted] = cache->entries.try_emplace(source);
    if (inserted) {
        iter->second.environment_map = create_environment_map(images, source);
    }
    return iter->second.environment_map;
}

void generated_equirect_env_map_to_env_map(
    Query<const GeneratedEquirectEnvironmentMap, EnvironmentMap> existing_maps,
    Query<Entity, const GeneratedEquirectEnvironmentMap>::Filter<
        Without<EnvironmentMap>> new_maps,
    ResRW<Assets<Image>> images,
    ResRW<EnvironmentMapCache> cache,
    Commands commands
) {
    for (auto [generated, environment_map] : existing_maps) {
        auto source = generated.equirect_image.id();
        if (environment_map->source_equirect_image != source) {
            environment_map =
                get_or_create_environment_map(source, images, cache);
        }
    }

    for (auto [entity, generated] : new_maps) {
        commands.entity(entity).add(get_or_create_environment_map(
            generated.equirect_image.id(),
            images,
            cache
        ));
    }
}

void insert_gpu_env_map(
    Query<const EnvironmentMap, GpuEnvironmentMap> existing_maps,
    Query<Entity, const EnvironmentMap>::Filter<Without<GpuEnvironmentMap>>
        new_maps,
    ResRO<RenderAssets<GpuImage>> gpu_images,
    Commands commands
) {
    auto prepare = [&](const EnvironmentMap& env_map,
                       GpuEnvironmentMap& gpu_env_map) {
        auto irradiance_gpu_image =
            gpu_images->get(env_map.irradiance_cubemap.id());
        auto radiance_gpu_image =
            gpu_images->get(env_map.radiance_cubemap.id());
        if (!irradiance_gpu_image || !radiance_gpu_image) {
            return false;
        }
        gpu_env_map.irradiance_cubemap = *irradiance_gpu_image;
        gpu_env_map.radiance_cubemap = *radiance_gpu_image;
        return true;
    };

    for (auto [env_map, gpu_env_map] : existing_maps) {
        prepare(env_map, gpu_env_map.write());
    }

    for (auto [entity, env_map] : new_maps) {
        GpuEnvironmentMap gpu_env_map;
        if (prepare(env_map, gpu_env_map)) {
            commands.entity(entity).add(std::move(gpu_env_map));
        }
    }
}

void convert_equirect_to_cubemap(
    Query<Entity, const GeneratedEquirectEnvironmentMap, GpuEnvironmentMap>
        query,
    ResRO<GraphicsDevice> device,
    ResRO<Assets<Image>> images,
    ResRW<EquirectToCubemap> equirect_to_cubemap
) {
    for (auto [entity, gen_env_map, gpu_env_map] : query) {
        (void)entity;
        auto environment_cubemap = equirect_to_cubemap->get_or_create_cubemap(
            *device,
            *images,
            gen_env_map.equirect_image
        );
        if (!environment_cubemap) {
            continue;
        }
        gpu_env_map->environment_cubemap = *environment_cubemap;
    }
}

void setup_environment_map_generation_resources(
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    ResRW<EnvironmentMapGenerationResources> resources
) {
    auto irradiance_shader = shader_cache->get_or_compile(
        AssetPath("shader://pbr/cubemap2irradiance.slang"),
        ShaderStages::Compute,
        {}
    );
    auto radiance_shader = shader_cache->get_or_compile(
        AssetPath("shader://pbr/cubemap2radiance.slang"),
        ShaderStages::Compute,
        {}
    );
    resources->irradiance_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            ShaderStages::Compute,
            {
                texture_read_only("cubemap"),
                sampler("cubemap_sampler"),
                texture_read_write("output_texture"),
            }
        )
    );
    resources->radiance_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            ShaderStages::Compute,
            {
                texture_read_only("cubemap"),
                sampler("cubemap_sampler"),
                texture_read_write("output_texture"),
                uniform_buffer("constants"),
            }
        )
    );
    resources->irradiance_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = irradiance_shader,
            .resource_layouts = {resources->irradiance_layout},
        }
    );
    resources->radiance_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = radiance_shader,
            .resource_layouts = {resources->radiance_layout},
        }
    );
    resources->cubemap_sampler =
        device->create_sampler(SamplerDescription::Linear);
}

void invalidate_environment_map_cache(
    EventReader<AssetEvent<Image>> image_events,
    ResRW<EquirectToCubemap> equirect_to_cubemap,
    ResRW<EnvironmentMapCache> cache
) {
    for (auto event = image_events.next(); event; event = image_events.next()) {
        if (event->type == AssetEventType::Added) {
            continue;
        }
        equirect_to_cubemap->invalidate(event->id);
        auto entry = cache->entries.find(event->id);
        if (entry == cache->entries.end()) {
            continue;
        }
        if (event->type == AssetEventType::Modified) {
            entry->second.generated_from_texture = nullptr;
        } else {
            cache->entries.erase(entry);
        }
    }
}

void generate_env_maps(
    Query<const GeneratedEquirectEnvironmentMap, const GpuEnvironmentMap> query,
    ResRO<GraphicsDevice> device,
    ResRO<EnvironmentMapGenerationResources> resources,
    ResRW<EnvironmentMapCache> cache
) {
    if (!resources->valid()) {
        return;
    }

    for (auto [generated, gpu_env_map] : query) {
        auto cache_entry = cache->entries.find(generated.equirect_image.id());
        if (cache_entry == cache->entries.end()) {
            continue;
        }
        auto source = gpu_env_map.environment_cubemap.texture();
        auto irradiance = gpu_env_map.irradiance_cubemap.texture();
        auto radiance = gpu_env_map.radiance_cubemap.texture();
        if (!source || !irradiance || !radiance ||
            cache_entry->second.generated_from_texture == source.get()) {
            continue;
        }

        auto command_buffer = device->create_command_buffer();
        command_buffer->begin();

        auto irradiance_view = device->create_texture_view(
            TextureViewDescription {
                .target = irradiance,
                .base_mip_level = 0,
                .mip_levels = 1,
                .base_array_layer = 0,
                .array_layers = 1,
                .view_type = TextureViewType::Texture2DArray,
            }
        );
        auto irradiance_set = device->create_resource_set(
            ResourceSetDescription {
                .layout = resources->irradiance_layout,
                .resources =
                    {source, resources->cubemap_sampler, irradiance_view},
                .name = "environment.irradiance",
            }
        );
        command_buffer->set_compute_pipeline(resources->irradiance_pipeline);
        command_buffer->set_resource_set(0, irradiance_set);
        command_buffer->dispatch(
            (irradiance->width() + 7) / 8,
            (irradiance->height() + 7) / 8,
            6
        );

        command_buffer->set_compute_pipeline(resources->radiance_pipeline);
        auto num_mips = std::max(radiance->mip_level(), 1U);
        for (uint32 level = 0; level < num_mips; ++level) {
            auto size = std::max(radiance->width() >> level, 1U);
            auto texture_view = device->create_texture_view(
                TextureViewDescription {
                    .target = radiance,
                    .base_mip_level = level,
                    .mip_levels = 1,
                    .base_array_layer = 0,
                    .array_layers = 1,
                    .view_type = TextureViewType::Texture2DArray,
                }
            );
            FilteringConstants uniform {
                .roughness = num_mips > 1 ?
                                 static_cast<float>(level) /
                                     static_cast<float>(num_mips - 1) :
                                 0.0f,
            };
            // Each dispatch owns its constants until the submitted command
            // buffer completes. Reusing one destination buffer would allow a
            // later transfer to overwrite constants still read by Vulkan.
            auto filtering_uniform_buffer = device->create_buffer(
                BufferDescription {
                    .size = sizeof(FilteringConstants),
                    .usages = BufferUsages::Uniform,
                }
            );
            command_buffer->update_buffer(
                filtering_uniform_buffer,
                &uniform,
                sizeof(uniform)
            );
            auto radiance_set = device->create_resource_set(
                ResourceSetDescription {
                    .layout = resources->radiance_layout,
                    .resources =
                        {source,
                         resources->cubemap_sampler,
                         texture_view,
                         filtering_uniform_buffer},
                    .name = "environment.radiance",
                }
            );
            command_buffer->set_resource_set(0, radiance_set);
            command_buffer->dispatch((size + 7) / 8, (size + 7) / 8, 6);
        }

        command_buffer->end();
        device->submit_commands(command_buffer);
        cache_entry->second.generated_from_texture = source.get();
    }
}

void EnvironmentMapPlugin::setup(App& app) {
    app.add_resource(EnvironmentMapCache {})
        .add_resource(EnvironmentMapGenerationResources {})
        .add_systems(Update, generated_equirect_env_map_to_env_map)
        .add_systems(StartUp, setup_environment_map_generation_resources)
        .add_systems(
            RenderUpdate,
            chain(
                invalidate_environment_map_cache,
                insert_gpu_env_map,
                convert_equirect_to_cubemap,
                generate_env_maps
            ) | in_set<RenderingSystems::PrepareResources>() |
                in_set<PbrSystems::PrepareEnvironmentMaps>()
        );
}

} // namespace fei
