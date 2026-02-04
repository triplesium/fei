#include "pbr/environment_map.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "pbr/cubemap.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/render_asset.hpp"

namespace fei {

void generated_equirect_env_map_to_env_map(
    Query<Entity, GeneratedEquirectEnvironmentMap>::Filter<
        Without<EnvironmentMap>> query,
    Res<Assets<Image>> images,
    Res<EquirectToCubemap> equirect_to_cubemap,
    Res<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, gen_env_map] : query) {
        auto irradiance_image = Image::create_empty(
            32,
            32,
            6,
            PixelFormat::Rgba32Float,
            {TextureUsage::Sampled, TextureUsage::Cubemap, TextureUsage::Storage
            },
            TextureType::Texture2D
        );
        auto irradiance_image_handle = images->add(std::move(irradiance_image));

        auto radiance_image = Image::create_empty(
            128,
            128,
            6,
            PixelFormat::Rgba32Float,
            {TextureUsage::Sampled, TextureUsage::Cubemap, TextureUsage::Storage
            },
            TextureType::Texture2D
        );
        auto radiance_image_handle = images->add(std::move(radiance_image));
        commands.entity(entity).add(EnvironmentMap {
            .irradiance_cubemap = irradiance_image_handle,
            .radiance_cubemap = radiance_image_handle,
        });
    }
}

void insert_gpu_env_map(
    Query<Entity, GeneratedEquirectEnvironmentMap, EnvironmentMap>::Filter<
        Without<GpuEnvironmentMap>> query,
    Res<Assets<Image>> images,
    Res<GraphicsDevice> device,
    Res<RenderAssets<GpuImage>> gpu_images,
    Commands commands
) {
    for (auto [entity, gen_env_map, env_map] : query) {
        auto equirect_gpu_image =
            gpu_images->get(gen_env_map.equirect_image.id());
        auto irradiance_gpu_image =
            gpu_images->get(env_map.irradiance_cubemap.id());
        auto radiance_gpu_image =
            gpu_images->get(env_map.radiance_cubemap.id());
        if (!irradiance_gpu_image || !radiance_gpu_image ||
            !equirect_gpu_image) {
            continue;
        }
        commands.entity(entity).add(GpuEnvironmentMap {
            .type = GpuEnvironmentMap::EnvMapType::Equirectmap,
            .environment_map = *equirect_gpu_image,
            .irradiance_cubemap = *irradiance_gpu_image,
            .radiance_cubemap = *radiance_gpu_image,
        });
    }
}

void generate_env_maps(
    Query<Entity, GpuEnvironmentMap>::Filter<
        Without<EnvironmentMapGeneratedTag>> query,
    Res<EquirectToCubemap> equirect_to_cubemap,
    Res<GraphicsDevice> device,
    Res<RenderAssets<GpuImage>> gpu_images,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shaders,
    Commands commands
) {
    for (auto [entity, gpu_env_map] : query) {
        if (gpu_env_map.type != GpuEnvironmentMap::EnvMapType::Equirectmap) {
            fatal("Unsupported EnvMapType in generate_env_maps");
            continue;
        }
        auto cubemap_texture = equirect_to_cubemap->convert_equirect_to_cubemap(
            *device,
            gpu_env_map.environment_map.texture()
        );
        auto cubemap_sampler =
            device->create_sampler(SamplerDescription::Linear);

        {
            auto shader_handle =
                asset_server->load<Shader>("embeded://cubemap2irradiance.comp");
            auto& shader = shaders->get(shader_handle).value();
            auto compute_shader =
                device->create_shader_module(shader.description());
            auto layout =
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
                             .name = "cubemap_sampler",
                             .kind = ResourceKind::Sampler,
                             .stages = {ShaderStages::Compute},
                         },
                         {
                             .binding = 1,
                             .name = "output_texture",
                             .kind = ResourceKind::TextureReadWrite,
                             .stages = {ShaderStages::Compute},
                         }},
                });
            auto pipeline =
                device->create_compute_pipeline(ComputePipelineDescription {
                    .shader = compute_shader,
                    .resource_layouts = {layout},
                });
            auto resource_set =
                device->create_resource_set(ResourceSetDescription {
                    .layout = layout,
                    .resources =
                        {cubemap_texture,
                         cubemap_sampler,
                         gpu_env_map.irradiance_cubemap.texture()},
                });
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

        commands.entity(entity).add(EnvironmentMapGeneratedTag {});
    }
}

void EnvironmentMapPlugin::setup(App& app) {
    app.add_systems(Update, generated_equirect_env_map_to_env_map)
        .add_systems(RenderStart, chain(insert_gpu_env_map, generate_env_maps));
}

} // namespace fei
