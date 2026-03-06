#include "pbr/mesh_view.hpp"

#include "core/camera.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "graphics/resource.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/light.hpp"
#include "pbr/lut.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/view.hpp"

namespace fei {

void init_mesh_view_layout(
    Res<GraphicsDevice> device,
    Res<MeshViewLayout> mesh_view_layout
) {
    mesh_view_layout->layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                uniform_buffer("View"),
                texture_read_only("irradiance_map"),
                texture_read_only("radiance_map"),
                sampler("cubemap_sampler"),
                texture_read_only("brdf_lut"),
                // uniform_buffer("Light"),
            }
        ));
}

void prepare_mesh_view_resource_set(
    Res<GraphicsDevice> device,
    Res<MeshViewLayout> mesh_view_layout,
    Res<GpuLUTs> luts,
    Query<GpuEnvironmentMap> query_env_maps,
    Query<Entity, ViewUniformBuffer> query_cameras,
    Query<DirectionalLight, Transform3d> query_directional_lights,
    Commands commands
) {
    auto [env_map] = query_env_maps.first();
    // auto [directional_light, light_transform] =
    //     query_directional_lights.first();
    // LightUniform light_uniform {
    //     .world_position = light_transform.position,
    //     .color = directional_light.color,
    // };
    // // TODO: cache
    // auto light_uniform_buffer = device->create_buffer(BufferDescription {
    //     .size = sizeof(LightUniform),
    //     .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
    // });
    // device->update_buffer(
    //     light_uniform_buffer,
    //     0,
    //     &light_uniform,
    //     sizeof(LightUniform)
    // );
    auto cubemap_sampler = device->create_sampler(SamplerDescription {
        .address_mode_u = SamplerAddressMode::ClampToEdge,
        .address_mode_v = SamplerAddressMode::ClampToEdge,
        .address_mode_w = SamplerAddressMode::ClampToEdge,
        .mag_filter = SamplerFilter::Linear,
        .min_filter = SamplerFilter::Linear,
        .mipmap_filter = SamplerFilter::Linear,
    });

    for (auto [entity, view_uniform_buffer_component] : query_cameras) {
        commands.entity(entity).add(MeshViewResourceSet {
            .resource_set = device->create_resource_set(ResourceSetDescription {
                .layout = mesh_view_layout->layout,
                .resources =
                    {
                        view_uniform_buffer_component.buffer,
                        env_map.irradiance_cubemap.texture(),
                        env_map.radiance_cubemap.texture(),
                        cubemap_sampler,
                        luts->brdf_lut.texture(),
                        // light_uniform_buffer,
                    },
            }),
        });
    }
}

} // namespace fei
