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
    const ViewUniformBuffer& view_uniform_buffer
) {
    return MeshViewResourceSet::Key {
        .layout = mesh_view_layout.layout.get(),
        .view_buffer = view_uniform_buffer.buffer.get(),
        .irradiance_map = env_map.irradiance_cubemap.texture().get(),
        .radiance_map = env_map.radiance_cubemap.texture().get(),
        .cubemap_sampler = mesh_view_layout.cubemap_sampler.get(),
        .brdf_lut = luts.brdf_lut.texture().get(),
    };
}

MeshViewResourceSet create_mesh_view_resource_set(
    const GraphicsDevice& device,
    const MeshViewLayout& mesh_view_layout,
    const GpuLUTs& luts,
    const GpuEnvironmentMap& env_map,
    const ViewUniformBuffer& view_uniform_buffer
) {
    auto irradiance_map = env_map.irradiance_cubemap.texture();
    auto radiance_map = env_map.radiance_cubemap.texture();
    auto brdf_lut = luts.brdf_lut.texture();
    auto key = mesh_view_resource_set_key(
        mesh_view_layout,
        luts,
        env_map,
        view_uniform_buffer
    );

    return MeshViewResourceSet {
        .resource_set = device.create_resource_set(
            ResourceSetDescription {
                .layout = mesh_view_layout.layout,
                .resources =
                    {
                        view_uniform_buffer.buffer,
                        irradiance_map,
                        radiance_map,
                        mesh_view_layout.cubemap_sampler,
                        brdf_lut,
                    },
                .name = "mesh_view",
            }
        ),
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
            {
                uniform_buffer("View"),
                texture_read_only("irradiance_map"),
                texture_read_only("radiance_map"),
                sampler("cubemap_sampler"),
                texture_read_only("brdf_lut"),
                // uniform_buffer("Light"),
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
}

void prepare_mesh_view_resource_set(
    ResRO<GraphicsDevice> device,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<GpuLUTs> luts,
    Query<const GpuEnvironmentMap> query_env_maps,
    Query<Entity, const ViewUniformBuffer, MeshViewResourceSet> query_cameras,
    Query<Entity, const ViewUniformBuffer>::Filter<Without<MeshViewResourceSet>>
        query_new_cameras,
    ResRW<MeshViewResourceSet> mesh_view_resource_set,
    Commands commands
) {
    auto [env_map] = query_env_maps.first();
    bool selected_resource_set = false;
    auto select_resource_set = [&](const MeshViewResourceSet& resource_set) {
        if (!selected_resource_set) {
            *mesh_view_resource_set = resource_set;
            selected_resource_set = true;
        }
    };

    for (auto [entity, view_uniform_buffer_component, view_resource_set] :
         query_cameras) {
        (void)entity;
        auto next_key = mesh_view_resource_set_key(
            *mesh_view_layout,
            *luts,
            env_map,
            view_uniform_buffer_component
        );
        if (view_resource_set.read().key != next_key) {
            view_resource_set = create_mesh_view_resource_set(
                *device,
                *mesh_view_layout,
                *luts,
                env_map,
                view_uniform_buffer_component
            );
        }
        select_resource_set(view_resource_set.read());
    }

    for (auto [entity, view_uniform_buffer_component] : query_new_cameras) {
        auto view_resource_set = create_mesh_view_resource_set(
            *device,
            *mesh_view_layout,
            *luts,
            env_map,
            view_uniform_buffer_component
        );
        select_resource_set(view_resource_set);
        commands.entity(entity).add(std::move(view_resource_set));
    }

    if (!selected_resource_set) {
        *mesh_view_resource_set = MeshViewResourceSet {};
    }
}

} // namespace fei
