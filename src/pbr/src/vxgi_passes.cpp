#include "pbr/vxgi.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace fei {

namespace {

struct VoxelDrawItem {
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    uint32 index_count {};
    uint32 vertex_count {};
    std::shared_ptr<const ResourceSet> mesh_set;
    std::shared_ptr<const ResourceSet> material_set;
    std::shared_ptr<Pipeline> pipeline;
};

std::vector<std::shared_ptr<const BindableResource>>
volume_bindings(const VxgiVolumes& volumes) {
    return {
        volumes.albedo,
        volumes.normal,
        volumes.emissive,
        volumes.radiance,
        volumes.static_flag,
    };
}

std::vector<std::shared_ptr<const BindableResource>>
voxelization_bindings(const VxgiVoxelization& voxelization) {
    return {voxelization.voxelization_uniform_buffer};
}

std::vector<std::shared_ptr<const BindableResource>>
accumulation_bindings(const VxgiVoxelization& voxelization) {
    return {
        voxelization.albedo_accumulation_buffer,
        voxelization.normal_accumulation_buffer,
        voxelization.emissive_accumulation_buffer,
        voxelization.count_accumulation_buffer,
    };
}

std::vector<std::shared_ptr<const BindableResource>> mipmap_base_bindings(
    const VxgiGenerateMipmapBase& mipmap_base,
    const VxgiVolumes& volumes
) {
    return {
        mipmap_base.uniform_buffer,
        volumes.radiance,
        mipmap_base.output_views[0],
        mipmap_base.output_views[1],
        mipmap_base.output_views[2],
        mipmap_base.output_views[3],
        mipmap_base.output_views[4],
        mipmap_base.output_views[5],
    };
}

std::vector<std::shared_ptr<const BindableResource>> mipmap_volume_bindings(
    const VxgiGenerateMipmapVolume& mipmap_volume,
    const VxgiGenerateMipmapVolume::MipEntry& entry
) {
    return {
        mipmap_volume.uniform_buffer,
        entry.dst_views[0],
        entry.dst_views[1],
        entry.dst_views[2],
        entry.dst_views[3],
        entry.dst_views[4],
        entry.dst_views[5],
        entry.src_views[0],
        entry.src_views[1],
        entry.src_views[2],
        entry.src_views[3],
        entry.src_views[4],
        entry.src_views[5],
    };
}

std::vector<std::shared_ptr<const BindableResource>> propagation_bindings(
    const VxgiInjectPropagation& propagation,
    const VxgiVolumes& volumes
) {
    return {
        propagation.uniform_buffer,
        volumes.radiance,
        volumes.albedo,
        volumes.normal,
        volumes.mipmap[0],
        volumes.mipmap[1],
        volumes.mipmap[2],
        volumes.mipmap[3],
        volumes.mipmap[4],
        volumes.mipmap[5],
        propagation.voxel_sampler,
    };
}

bool volumes_valid(const VxgiVolumes& volumes) {
    if (!volumes.albedo || !volumes.normal || !volumes.emissive ||
        !volumes.radiance || !volumes.static_flag) {
        return false;
    }
    for (const auto& mipmap : volumes.mipmap) {
        if (!mipmap) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<ResourceSet> cached_set(
    RenderResourceSetCache& cache,
    const GraphicsDevice& device,
    std::string name,
    const std::shared_ptr<ResourceLayout>& layout,
    std::vector<std::shared_ptr<const BindableResource>> bindings
) {
    return cache
        .get_or_create(device, std::move(name), layout, std::move(bindings));
}

void render_mipmap_base(
    CommandBuffer& commands,
    RenderResourceSetCache& cache,
    const GraphicsDevice& device,
    const VxgiVolumes& volumes,
    const VxgiGenerateMipmapBase& mipmap_base
) {
    auto resource_set = cached_set(
        cache,
        device,
        "vxgi.mipmap_base",
        mipmap_base.resource_layout,
        mipmap_base_bindings(mipmap_base, volumes)
    );
    if (!resource_set || !mipmap_base.pipeline) {
        return;
    }
    commands.set_compute_pipeline(mipmap_base.pipeline);
    commands.set_resource_set(0, std::move(resource_set));
    const auto work_groups = (volumes.config.voxel_resolution / 2) / 8;
    commands.dispatch(work_groups, work_groups, work_groups);
}

void render_mipmap_volume(
    CommandBuffer& commands,
    RenderResourceSetCache& cache,
    const GraphicsDevice& device,
    const VxgiGenerateMipmapVolume& mipmap_volume
) {
    if (!mipmap_volume.pipeline || !mipmap_volume.uniform_buffer) {
        return;
    }
    commands.set_compute_pipeline(mipmap_volume.pipeline);
    for (const auto& entry : mipmap_volume.mip_entries) {
        auto resource_set = cached_set(
            cache,
            device,
            "vxgi.mipmap_volume",
            mipmap_volume.resource_layout,
            mipmap_volume_bindings(mipmap_volume, entry)
        );
        if (!resource_set) {
            continue;
        }
        VxgiGenerateMipmapVolume::Uniform uniform {
            .mip_dimension = Vector3 {static_cast<float>(entry.mip_dimension)},
            .mip_level = static_cast<int>(entry.mip_level),
        };
        commands.update_buffer(
            mipmap_volume.uniform_buffer,
            &uniform,
            sizeof(uniform)
        );
        commands.set_resource_set(0, std::move(resource_set));
        const auto work_groups = (entry.mip_dimension + 7) / 8;
        commands.dispatch(work_groups, work_groups, work_groups);
    }
}

} // namespace

void render_vxgi_voxelization_pass(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    ResRW<VxgiVoxelization> voxelization,
    ResRW<VxgiVolumes> volumes,
    ResRW<MeshMaterialPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device
) {
    auto* commands = frame->command_buffer();
    if (!commands || !voxelization->dirty || !volumes_valid(*volumes)) {
        return;
    }

    std::vector<VoxelDrawItem> draw_items;
    for (auto [entity, mesh, mesh_material, transform] : query_meshes) {
        (void)transform;
        auto gpu_mesh = gpu_meshes->get(mesh.mesh);
        auto material = materials->get(mesh_material.material);
        auto mesh_uniform = mesh_uniforms->entries.find(entity);
        if (!gpu_mesh || !material ||
            mesh_uniform == mesh_uniforms->entries.end()) {
            return;
        }
        auto pipeline_id = pipelines->find(
            entity,
            *material,
            *gpu_mesh,
            voxelization->pipeline_specializer
        );
        if (!pipeline_id) {
            return;
        }
        auto pipeline = pipeline_cache->get_render_pipeline(*pipeline_id);
        if (!pipeline) {
            return;
        }
        auto index = gpu_mesh->index_buffer();
        draw_items.push_back(
            VoxelDrawItem {
                .vertex_buffer = gpu_mesh->vertex_buffer(),
                .index_buffer = index ? *index : nullptr,
                .index_count = static_cast<uint32>(
                    gpu_mesh->index_buffer_size() / sizeof(std::uint32_t)
                ),
                .vertex_count = static_cast<uint32>(gpu_mesh->vertex_count()),
                .mesh_set = mesh_uniform->second.resource_set,
                .material_set = material->resource_set(),
                .pipeline = std::move(pipeline),
            }
        );
    }

    auto volumes_set = cached_set(
        *resource_sets,
        *device,
        "vxgi.volumes",
        volumes->resource_layout,
        volume_bindings(*volumes)
    );
    auto voxelization_set = cached_set(
        *resource_sets,
        *device,
        "vxgi.voxelization",
        voxelization->resource_layout,
        voxelization_bindings(*voxelization)
    );
    auto accumulation_set = cached_set(
        *resource_sets,
        *device,
        "vxgi.accumulation",
        voxelization->accumulation_layout,
        accumulation_bindings(*voxelization)
    );
    if (!volumes_set || !voxelization_set || !accumulation_set ||
        !voxelization->clear_pipeline || !voxelization->resolve_pipeline) {
        return;
    }

    const auto work_groups = (volumes->config.voxel_resolution + 7) / 8;
    commands->set_compute_pipeline(voxelization->clear_pipeline);
    commands->set_resource_set(0, volumes_set);
    commands->set_resource_set(1, voxelization_set);
    commands->set_resource_set(2, accumulation_set);
    commands->dispatch(work_groups, work_groups, work_groups);

    if (!draw_items.empty() && voxelization->temp_texture) {
        commands->begin_render_pass(
            RenderPassDescription {
                .color_attachments = {
                    RenderPassColorAttachment {
                        .texture = voxelization->temp_texture,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
            }
        );
        commands->set_viewport(
            0,
            0,
            volumes->config.voxel_resolution,
            volumes->config.voxel_resolution
        );
        for (const auto& item : draw_items) {
            commands->set_render_pipeline(item.pipeline);
            commands->set_resource_set(1, item.mesh_set);
            commands->set_resource_set(2, item.material_set);
            commands->set_resource_set(3, volumes_set);
            commands->set_resource_set(4, voxelization_set);
            commands->set_resource_set(5, accumulation_set);
            commands->set_vertex_buffer(item.vertex_buffer);
            if (item.index_buffer) {
                commands->set_index_buffer(
                    item.index_buffer,
                    IndexFormat::Uint32
                );
                commands->draw_indexed(item.index_count);
            } else {
                commands->draw(0, item.vertex_count);
            }
        }
        commands->end_render_pass();

        commands->set_compute_pipeline(voxelization->resolve_pipeline);
        commands->set_resource_set(0, volumes_set);
        commands->set_resource_set(1, voxelization_set);
        commands->set_resource_set(2, accumulation_set);
        commands->dispatch(work_groups, work_groups, work_groups);
    }
    voxelization->dirty = false;
}

void render_vxgi_mipmap_base_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapBase> generate_mipmap_base,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device
) {
    if (auto* commands = frame->command_buffer()) {
        render_mipmap_base(
            *commands,
            *resource_sets,
            *device,
            *volumes,
            *generate_mipmap_base
        );
    }
}

void render_vxgi_mipmap_base_after_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapBase> generate_mipmap_base,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device
) {
    if (auto* commands = frame->command_buffer()) {
        render_mipmap_base(
            *commands,
            *resource_sets,
            *device,
            *volumes,
            *generate_mipmap_base
        );
    }
}

void render_vxgi_mipmap_volume_pass(
    ResRO<VxgiVolumes>,
    ResRO<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device
) {
    if (auto* commands = frame->command_buffer()) {
        render_mipmap_volume(
            *commands,
            *resource_sets,
            *device,
            *generate_mipmap_volume
        );
    }
}

void render_vxgi_mipmap_volume_after_propagation_pass(
    ResRO<VxgiVolumes>,
    ResRO<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device
) {
    if (auto* commands = frame->command_buffer()) {
        render_mipmap_volume(
            *commands,
            *resource_sets,
            *device,
            *generate_mipmap_volume
        );
    }
}

void render_vxgi_inject_radiance_pass(
    Query<const ShadowMap> query_shadow_maps,
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<VxgiInjectRadiance> inject_radiance,
    ResRO<LightingResources> lighting,
    ResRO<RenderingDefaults> rendering_defaults,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device
) {
    auto* commands = frame->command_buffer();
    if (!commands || !volumes_valid(*volumes)) {
        return;
    }
    std::shared_ptr<Texture> shadow_map;
    for (auto [candidate] : query_shadow_maps) {
        if (candidate.texture) {
            shadow_map = candidate.texture;
            break;
        }
    }
    auto volumes_set = cached_set(
        *resource_sets,
        *device,
        "vxgi.volumes",
        volumes->resource_layout,
        volume_bindings(*volumes)
    );
    auto voxelization_set = cached_set(
        *resource_sets,
        *device,
        "vxgi.voxelization",
        voxelization->resource_layout,
        voxelization_bindings(*voxelization)
    );
    auto lighting_set = cached_set(
        *resource_sets,
        *device,
        "lighting",
        lighting->resource_layout,
        {
            lighting->uniform_buffer,
            shadow_map ? shadow_map : rendering_defaults->default_texture,
            lighting->shadow_map_sampler,
        }
    );
    auto inject_set = cached_set(
        *resource_sets,
        *device,
        "vxgi.inject_radiance",
        inject_radiance->resource_layout,
        {inject_radiance->uniform_buffer}
    );
    if (!volumes_set || !voxelization_set || !lighting_set || !inject_set ||
        !inject_radiance->pipeline) {
        return;
    }
    commands->set_compute_pipeline(inject_radiance->pipeline);
    commands->set_resource_set(0, std::move(volumes_set));
    commands->set_resource_set(1, std::move(voxelization_set));
    commands->set_resource_set(2, std::move(lighting_set));
    commands->set_resource_set(3, std::move(inject_set));
    const auto work_groups = volumes->config.voxel_resolution / 8;
    commands->dispatch(work_groups, work_groups, work_groups);
}

void render_vxgi_inject_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiInjectPropagation> inject_propagation,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device
) {
    auto* commands = frame->command_buffer();
    if (!commands || !volumes_valid(*volumes)) {
        return;
    }
    auto resource_set = cached_set(
        *resource_sets,
        *device,
        "vxgi.inject_propagation",
        inject_propagation->resource_layout,
        propagation_bindings(*inject_propagation, *volumes)
    );
    if (!resource_set || !inject_propagation->pipeline) {
        return;
    }
    commands->set_compute_pipeline(inject_propagation->pipeline);
    commands->set_resource_set(0, std::move(resource_set));
    const auto work_groups = volumes->config.voxel_resolution / 8;
    commands->dispatch(work_groups, work_groups, work_groups);
}

} // namespace fei
