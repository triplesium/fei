#include "pbr/mesh_view.hpp"

#include "ecs/query.hpp"
#include "graphics/resource.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/lut.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/view.hpp"

namespace fei {

namespace {

MeshViewResourceSet::Key mesh_view_resource_set_key(
    const MeshViewLayout& mesh_view_layout,
    const GpuLUTs& luts,
    const GpuEnvironmentMap& env_map,
    const ViewUniformBuffer& view_uniform_buffer,
    const Buffer& environment_uniform_buffer
) {
    return MeshViewResourceSet::Key {
        .layout = mesh_view_layout.layout.get(),
        .environment_layout = mesh_view_layout.environment_layout.get(),
        .view_buffer = view_uniform_buffer.buffer.get(),
        .irradiance_map = env_map.irradiance_cubemap.texture().get(),
        .radiance_map = env_map.radiance_cubemap.texture().get(),
        .cubemap_sampler = mesh_view_layout.cubemap_sampler.get(),
        .brdf_lut = luts.brdf_lut.texture().get(),
        .brdf_sampler = mesh_view_layout.brdf_sampler.get(),
        .environment_uniform_buffer = &environment_uniform_buffer,
    };
}

MeshViewResourceSet create_mesh_view_resource_set(
    const GraphicsDevice& device,
    const MeshViewLayout& mesh_view_layout,
    const GpuLUTs& luts,
    const GpuEnvironmentMap& env_map,
    const ViewUniformBuffer& view_uniform_buffer,
    std::shared_ptr<Buffer> environment_uniform_buffer
) {
    auto irradiance_map = env_map.irradiance_cubemap.texture();
    auto radiance_map = env_map.radiance_cubemap.texture();
    auto brdf_lut = luts.brdf_lut.texture();
    auto key = mesh_view_resource_set_key(
        mesh_view_layout,
        luts,
        env_map,
        view_uniform_buffer,
        *environment_uniform_buffer
    );

    return MeshViewResourceSet {
        .resource_set = device.create_resource_set(
            ResourceSetDescription {
                .layout = mesh_view_layout.layout,
                .resources = {view_uniform_buffer.buffer},
                .name = "mesh_view",
            }
        ),
        .environment_resource_set = device.create_resource_set(
            ResourceSetDescription {
                .layout = mesh_view_layout.environment_layout,
                .resources =
                    {
                        environment_uniform_buffer,
                        irradiance_map,
                        radiance_map,
                        mesh_view_layout.cubemap_sampler,
                        brdf_lut,
                        mesh_view_layout.brdf_sampler,
                    },
                .name = "environment",
            }
        ),
        .environment_uniform_buffer = std::move(environment_uniform_buffer),
        .key = key,
    };
}

} // namespace

void init_mesh_view_layout(
    ResRO<GraphicsDevice> device,
    ResRW<MeshViewLayout> mesh_view_layout
) {
    mesh_view_layout->layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {uniform_buffer("View")}
        )
    );
    mesh_view_layout->environment_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {
                uniform_buffer("environment"),
                texture_read_only("irradiance_map"),
                texture_read_only("radiance_map"),
                sampler("cubemap_sampler"),
                texture_read_only("brdf_lut"),
                sampler("brdf_sampler"),
            }
        )
    );
    mesh_view_layout->cubemap_sampler = device->create_sampler(
        SamplerDescription {
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge,
            .mag_filter = SamplerFilter::Linear,
            .min_filter = SamplerFilter::Linear,
            .mipmap_filter = SamplerFilter::Linear,
        }
    );
    mesh_view_layout->brdf_sampler = device->create_sampler(
        SamplerDescription {
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge,
            .mag_filter = SamplerFilter::Linear,
            .min_filter = SamplerFilter::Linear,
            .mipmap_filter = SamplerFilter::Linear,
        }
    );
}

void prepare_mesh_view_resource_set(
    ResRO<GraphicsDevice> device,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<GpuLUTs> luts,
    ResRO<RenderQueue> render_queue,
    Query<
        Entity,
        const ViewUniformBuffer,
        const GpuEnvironmentMap,
        const EnvironmentMapLight,
        MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    Query<
        Entity,
        const ViewUniformBuffer,
        const GpuEnvironmentMap,
        const EnvironmentMapLight>::
        Filter<With<Camera3d>, Without<MeshViewResourceSet>> query_new_cameras,
    Query<Entity, const ViewUniformBuffer, MeshViewResourceSet>::Filter<
        Without<Camera3d>> query_views,
    Query<Entity, const ViewUniformBuffer>::
        Filter<Without<Camera3d>, Without<MeshViewResourceSet>> query_new_views,
    ResRW<MeshViewResourceSet> mesh_view_resource_set,
    Commands commands
) {
    bool selected_resource_set = false;
    const GpuEnvironmentMap* fallback_env_map = nullptr;
    const EnvironmentMapLight* fallback_environment_light = nullptr;
    auto select_resource_set = [&](const MeshViewResourceSet& resource_set) {
        if (!selected_resource_set) {
            *mesh_view_resource_set = resource_set;
            selected_resource_set = true;
        }
    };

    auto prepare = [&](const ViewUniformBuffer& view_uniform_buffer,
                       const GpuEnvironmentMap& env_map,
                       const EnvironmentMapLight& environment_light,
                       MeshViewResourceSet& view_resource_set) {
        if (!view_resource_set.environment_uniform_buffer) {
            view_resource_set.environment_uniform_buffer =
                device->create_buffer(
                    BufferDescription {
                        .size = sizeof(EnvironmentMapUniform),
                        .usages = BufferUsages::Uniform,
                    }
                );
        }

        auto radiance_map = env_map.radiance_cubemap.texture();
        auto radiance_mip_levels = radiance_map ? radiance_map->mip_level() : 0;
        auto uniform = EnvironmentMapUniform {
            .environment_from_world =
                environment_light.rotation.inversed().to_matrix(),
            .intensity = environment_light.intensity,
            .max_specular_lod =
                radiance_mip_levels > 0 ?
                    static_cast<float>(radiance_mip_levels - 1) :
                    0.0f,
            .enabled = environment_light.enabled ? 1U : 0U,
        };
        render_queue->write_buffer(
            view_resource_set.environment_uniform_buffer,
            0,
            &uniform,
            sizeof(uniform)
        );

        auto next_key = mesh_view_resource_set_key(
            *mesh_view_layout,
            *luts,
            env_map,
            view_uniform_buffer,
            *view_resource_set.environment_uniform_buffer
        );
        if (view_resource_set.key != next_key) {
            view_resource_set = create_mesh_view_resource_set(
                *device,
                *mesh_view_layout,
                *luts,
                env_map,
                view_uniform_buffer,
                view_resource_set.environment_uniform_buffer
            );
        }
        select_resource_set(view_resource_set);
    };

    for (auto
         [entity,
          view_uniform_buffer,
          env_map,
          environment_light,
          view_resource_set] : query_cameras) {
        (void)entity;
        if (!fallback_env_map) {
            fallback_env_map = &env_map;
            fallback_environment_light = &environment_light;
        }
        prepare(
            view_uniform_buffer,
            env_map,
            environment_light,
            view_resource_set.write()
        );
    }

    for (auto [entity, view_uniform_buffer, env_map, environment_light] :
         query_new_cameras) {
        if (!fallback_env_map) {
            fallback_env_map = &env_map;
            fallback_environment_light = &environment_light;
        }
        MeshViewResourceSet view_resource_set;
        prepare(
            view_uniform_buffer,
            env_map,
            environment_light,
            view_resource_set
        );
        commands.entity(entity).add(std::move(view_resource_set));
    }

    if (fallback_env_map && fallback_environment_light) {
        for (auto [entity, view_uniform_buffer, view_resource_set] :
             query_views) {
            (void)entity;
            prepare(
                view_uniform_buffer,
                *fallback_env_map,
                *fallback_environment_light,
                view_resource_set.write()
            );
        }

        for (auto [entity, view_uniform_buffer] : query_new_views) {
            MeshViewResourceSet view_resource_set;
            prepare(
                view_uniform_buffer,
                *fallback_env_map,
                *fallback_environment_light,
                view_resource_set
            );
            commands.entity(entity).add(std::move(view_resource_set));
        }
    }

    if (!selected_resource_set) {
        *mesh_view_resource_set = MeshViewResourceSet {};
    }
}

} // namespace fei
