#include "pbr/vxgi.hpp"

#include "base/hash.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fei {

namespace {

struct VxgiVoxelDrawItem {
    const GpuMesh* gpu_mesh;
    const PreparedMaterial* material;
    std::shared_ptr<ResourceSet> mesh_set;
    std::shared_ptr<Pipeline> pipeline;
};

template<class QueryT>
bool collect_vxgi_voxel_draw_items(
    QueryT&& query_meshes,
    const VxgiVoxelization& voxelization,
    MeshMaterialPipelines& pipelines,
    const PipelineCache& pipeline_cache,
    const RenderAssets<GpuMesh>& gpu_meshes,
    const RenderAssets<PreparedMaterial>& materials,
    const MeshUniforms& mesh_uniforms,
    std::vector<VxgiVoxelDrawItem>& draw_items
) {
    draw_items.clear();

    for (const auto& [entity, mesh3d, mesh_material3d, transform3d] :
         query_meshes) {
        auto gpu_mesh_opt = gpu_meshes.get(mesh3d.mesh);
        auto material_opt = materials.get(mesh_material3d.material);
        auto mesh_uniform_it = mesh_uniforms.entries.find(entity);
        if (!gpu_mesh_opt || !material_opt ||
            mesh_uniform_it == mesh_uniforms.entries.end()) {
            return false;
        }

        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        auto pipeline_id = pipelines.find(
            entity,
            material,
            gpu_mesh,
            voxelization.pipeline_specializer
        );
        if (!pipeline_id) {
            return false;
        }
        auto pipeline = pipeline_cache.get_render_pipeline(*pipeline_id);
        if (!pipeline) {
            return false;
        }

        draw_items.push_back(
            VxgiVoxelDrawItem {
                .gpu_mesh = &gpu_mesh,
                .material = &material,
                .mesh_set = mesh_uniform_it->second.resource_set,
                .pipeline = std::move(pipeline),
            }
        );
    }

    return true;
}

} // namespace

VxgiVoxelizationSpecializer::VxgiVoxelizationSpecializer(
    std::vector<std::shared_ptr<const ShaderModule>> shader_modules,
    std::shared_ptr<const ResourceLayout> volumes_layout,
    std::shared_ptr<const ResourceLayout> voxelization_layout
) :
    m_shader_modules(std::move(shader_modules)),
    m_volumes_layout(std::move(volumes_layout)),
    m_voxelization_layout(std::move(voxelization_layout)) {
    for (const auto& shader_module : m_shader_modules) {
        hash_combine(m_cache_key, shader_module.get());
    }
    hash_combine(m_cache_key, m_volumes_layout.get());
    hash_combine(m_cache_key, m_voxelization_layout.get());
}

void VxgiVoxelizationSpecializer::specialize(
    RenderPipelineDescription& desc,
    const GpuMesh& /*mesh*/,
    const PreparedMaterial& /*material*/
) const {
    desc.shader_program.shaders = m_shader_modules;
    desc.depth_stencil_state = DepthStencilStateDescription::Disabled;
    desc.rasterizer_state.cull_mode = CullMode::None;
    desc.resource_layouts.push_back(m_volumes_layout);
    desc.resource_layouts.push_back(m_voxelization_layout);
}

void setup_vxgi(
    ResRW<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shader_assets,
    Commands commands
) {
    const auto& config = volumes->config;
    TextureDescription desc {
        .width = config.voxel_resolution,
        .height = config.voxel_resolution,
        .depth = config.voxel_resolution,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = {TextureUsage::Sampled, TextureUsage::Storage},
        .texture_type = TextureType::Texture3D,
    };
    volumes->albedo = device->create_texture(desc);
    volumes->normal = device->create_texture(desc);
    volumes->emissive = device->create_texture(desc);
    volumes->radiance = device->create_texture(desc);
    volumes->static_flag = device->create_texture(desc);

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    for (int i = 0; i < 6; ++i) {
        volumes->mipmap[i] = device->create_texture(
            TextureDescription {
                .width = config.voxel_resolution / 2,
                .height = config.voxel_resolution / 2,
                .depth = config.voxel_resolution / 2,
                .mip_level = static_cast<uint32>(
                    std::floor(std::log2(config.voxel_resolution))
                ),
                .layer = 1,
                .texture_format = PixelFormat::Rgba8Unorm,
                .texture_usage =
                    {TextureUsage::Sampled,
                     TextureUsage::Storage,
                     TextureUsage::GenerateMipmaps},
                .texture_type = TextureType::Texture3D,
            }
        );
        command_buffer->generate_mipmaps(volumes->mipmap[i]);
    }
    command_buffer->end();
    device->submit_commands(command_buffer);

    volumes->resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex,
             ShaderStages::Geometry,
             ShaderStages::Fragment},
            {
                texture_read_write("voxel_albedo"),
                texture_read_write("voxel_normal"),
                texture_read_write("voxel_emissive"),
                texture_read_write("voxel_radiance"),
                texture_read_write("static_voxel_flag"),
            }
        )
    );

    volumes->resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = volumes->resource_layout,
            .resources = {
                volumes->albedo,
                volumes->normal,
                volumes->emissive,
                volumes->radiance,
                volumes->static_flag,
            },
        }
    );

    std::vector<Handle<Shader>> shader_handles {
        asset_server->load<Shader>("shader://voxelization.vert"),
        asset_server->load<Shader>("shader://voxelization.geom"),
        asset_server->load<Shader>("shader://voxelization.frag"),
    };
    std::vector<std::shared_ptr<const ShaderModule>> shader_modules;
    for (const auto& handle : shader_handles) {
        auto shader = shader_assets->get(handle).value();
        shader_modules.push_back(
            device->create_shader_module(shader.description())
        );
    }

    auto voxelization_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex,
             ShaderStages::Geometry,
             ShaderStages::Fragment,
             ShaderStages::Compute},
            {uniform_buffer("VxgiVoxelization")}
        )
    );

    auto voxelization_uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiVoxelizationUniform),
            .usages = BufferUsages::Uniform,
        }
    );

    commands.add_resource(
        VxgiVoxelization {
            .voxelization_uniform_buffer = voxelization_uniform_buffer,
            .temp_texture = device->create_texture(
                TextureDescription {
                    .width = 1920,
                    .height = 1080,
                    .depth = 1,
                    .mip_level = 1,
                    .layer = 1,
                    .texture_format = PixelFormat::Rgba8Unorm,
                    .texture_usage = TextureUsage::RenderTarget,
                    .texture_type = TextureType::Texture2D,
                }
            ),
            .resource_layout = voxelization_resource_layout,
            .resource_set = device->create_resource_set(
                ResourceSetDescription {
                    .layout = voxelization_resource_layout,
                    .resources = {voxelization_uniform_buffer},
                }
            ),
            .pipeline_specializer = VxgiVoxelizationSpecializer(
                shader_modules,
                volumes->resource_layout,
                voxelization_resource_layout
            ),
        }
    );
}

void compute_scene_aabb(
    ResRW<VxgiVoxelization> voxelization,
    Query<const Mesh3d, const Transform3d, const Aabb> query
) {
    if (query.empty()) {
        voxelization->scene_aabb = Aabb {
            .min = {0.0f, 0.0f, 0.0f},
            .max = {1.0f, 1.0f, 1.0f},
        };
        return;
    }
    Vector3 min_point {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vector3 max_point {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (const auto& [mesh3d, transform3d, aabb] : query) {
        auto min = transform3d.position + aabb.min;
        auto max = transform3d.position + aabb.max;
        min_point.x = std::min(min_point.x, min.x);
        min_point.y = std::min(min_point.y, min.y);
        min_point.z = std::min(min_point.z, min.z);
        max_point.x = std::max(max_point.x, max.x);
        max_point.y = std::max(max_point.y, max.y);
        max_point.z = std::max(max_point.z, max.z);
    }
    voxelization->scene_aabb = Aabb {
        .min = {min_point.x, min_point.y, min_point.z},
        .max = {max_point.x, max_point.y, max_point.z},
    };
}

void prepare_vxgi_voxelization(
    ResRW<VxgiVoxelization> voxelization,
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device
) {
    auto axis_size = voxelization->scene_aabb.extent() * 2.0f;
    auto center = voxelization->scene_aabb.center();
    auto volume_grid_size = std::max({axis_size.x, axis_size.y, axis_size.z});
    VxgiVoxelizationUniform uniform {};
    auto voxel_size =
        volume_grid_size / static_cast<float>(volumes->config.voxel_resolution);
    auto half_size = volume_grid_size * 0.5f;
    auto proj = orthographic(
        -half_size,
        half_size,
        half_size,
        -half_size,
        0.0f,
        volume_grid_size
    );
    uniform.view_projections[0] =
        proj *
        look_at(center + Vector3::Right * half_size, center, Vector3::Up);
    uniform.view_projections[1] =
        proj * look_at(center + Vector3::Left * half_size, center, Vector3::Up);
    uniform.view_projections[2] =
        proj *
        look_at(center + Vector3::Up * half_size, center, Vector3::Forward);
    for (int i = 0; i < 3; ++i) {
        uniform.inv_view_projections[i] =
            uniform.view_projections[i].inverse_affine();
    }
    uniform.volume_dimension = volumes->config.voxel_resolution;
    uniform.flag_static_voxels = 1;
    uniform.voxel_scale = 1.0f / volume_grid_size;
    uniform.voxel_size = voxel_size;
    auto cube_half_extent = Vector3 {half_size, half_size, half_size};
    uniform.world_min_point = center - cube_half_extent;
    device->update_buffer(
        voxelization->voxelization_uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiVoxelizationUniform)
    );
}

void mark_vxgi_voxelization_dirty(
    ResRW<VxgiVoxelization> voxelization,
    EventReader<SceneSpawnedEvent> spawn_events
) {
    bool dirty = false;
    while (spawn_events.next()) {
        dirty = true;
    }
    if (dirty) {
        voxelization->dirty = true;
    }
}

void queue_vxgi_voxelization_pipelines(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    ResRW<VxgiVoxelization> voxelization,
    ResRW<MeshMaterialPipelines> pipelines,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials
) {
    if (!voxelization->dirty) {
        return;
    }

    for (const auto& [entity, mesh3d, mesh_material3d, transform3d] :
         query_meshes) {
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh);
        auto material_opt = materials->get(mesh_material3d.material);
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }

        pipelines->request(
            entity,
            *material_opt,
            *gpu_mesh_opt,
            voxelization->pipeline_specializer
        );
    }
}

void voxelize_scene(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    ResRW<VxgiVoxelization> voxelization,
    ResRW<VxgiVolumes> volumes,
    ResRW<MeshMaterialPipelines> pipelines,
    ResRW<PipelineCache> pipeline_cache,
    ResRO<GraphicsDevice> device,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<MeshUniforms> mesh_uniforms
) {
    if (!voxelization->dirty) {
        return;
    }

    std::vector<VxgiVoxelDrawItem> draw_items;
    // Keep the volume dirty until all draw inputs are ready, so a partial
    // voxelization cannot overwrite the previous complete result.
    if (!collect_vxgi_voxel_draw_items(
            query_meshes,
            *voxelization,
            *pipelines,
            *pipeline_cache,
            *gpu_meshes,
            *materials,
            *mesh_uniforms,
            draw_items
        ) ||
        draw_items.empty()) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
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
    command_buffer->set_viewport(
        0,
        0,
        volumes->config.voxel_resolution,
        volumes->config.voxel_resolution
    );
    for (const auto& item : draw_items) {
        auto& gpu_mesh = *item.gpu_mesh;
        auto& material = *item.material;

        command_buffer->set_render_pipeline(item.pipeline);
        command_buffer->set_resource_set(1, item.mesh_set);
        command_buffer->set_resource_set(2, material.resource_set());
        command_buffer->set_resource_set(3, volumes->resource_set);
        command_buffer->set_resource_set(4, voxelization->resource_set);
        command_buffer->set_vertex_buffer(gpu_mesh.vertex_buffer());
        if (auto index_buffer = gpu_mesh.index_buffer()) {
            command_buffer->set_index_buffer(
                *index_buffer,
                IndexFormat::Uint32
            );
            command_buffer->draw_indexed(
                gpu_mesh.index_buffer_size() / sizeof(std::uint32_t)
            );
        } else {
            command_buffer->draw(0, gpu_mesh.vertex_count());
        }
    }
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
    voxelization->dirty = false;
}

void setup_vxgi_generate_mipmap_base(
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("shader://aniso_mipmapbase.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Compute},
            {
                uniform_buffer("VxgiGenerateMipmapBase"),
                texture_read_only("voxel_base"),
                texture_read_write("voxel_mipmap[0]"),
                texture_read_write("voxel_mipmap[1]"),
                texture_read_write("voxel_mipmap[2]"),
                texture_read_write("voxel_mipmap[3]"),
                texture_read_write("voxel_mipmap[4]"),
                texture_read_write("voxel_mipmap[5]"),
            }
        )
    );
    auto pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = shader_module,
            .resource_layouts = {resource_layout},
        }
    );
    auto uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiGenerateMipmapBase::Uniform),
            .usages = BufferUsages::Uniform,
        }
    );
    VxgiGenerateMipmapBase::Uniform uniform {
        .mip_dimension = static_cast<int>(volumes->config.voxel_resolution / 2),
    };
    device->update_buffer(
        uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiGenerateMipmapBase::Uniform)
    );
    auto resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resource_layout,
            .resources = {
                uniform_buffer,
                volumes->radiance,
                volumes->mipmap[0],
                volumes->mipmap[1],
                volumes->mipmap[2],
                volumes->mipmap[3],
                volumes->mipmap[4],
                volumes->mipmap[5],
            },
        }
    );

    commands.add_resource(
        VxgiGenerateMipmapBase {
            .pipeline = pipeline,
            .resource_layout = resource_layout,
            .resource_set = resource_set,
        }
    );
}

void generate_mipmap_base(
    ResRW<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapBase> generate_mipmap_base,
    ResRO<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(generate_mipmap_base->pipeline);
    command_buffer->set_resource_set(0, generate_mipmap_base->resource_set);
    auto work_groups = (volumes->config.voxel_resolution / 2) / 8;
    command_buffer->dispatch(work_groups, work_groups, work_groups);
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void setup_vxgi_generate_mipmap_volume(
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("shader://aniso_mipmapvolume.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Compute},
            {
                uniform_buffer("VxgiGenerateMipmapVolume"),
                texture_read_write("voxel_mipmap_dst[0]"),
                texture_read_write("voxel_mipmap_dst[1]"),
                texture_read_write("voxel_mipmap_dst[2]"),
                texture_read_write("voxel_mipmap_dst[3]"),
                texture_read_write("voxel_mipmap_dst[4]"),
                texture_read_write("voxel_mipmap_dst[5]"),
                texture_read_only("voxel_mipmap_src[0]"),
                texture_read_only("voxel_mipmap_src[1]"),
                texture_read_only("voxel_mipmap_src[2]"),
                texture_read_only("voxel_mipmap_src[3]"),
                texture_read_only("voxel_mipmap_src[4]"),
                texture_read_only("voxel_mipmap_src[5]"),
            }
        )
    );
    auto pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = shader_module,
            .resource_layouts = {resource_layout},
        }
    );
    auto uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiGenerateMipmapVolume::Uniform),
            .usages = BufferUsages::Uniform,
        }
    );
    commands.add_resource(
        VxgiGenerateMipmapVolume {
            .pipeline = pipeline,
            .resource_layout = resource_layout,
            .uniform_buffer = uniform_buffer,
        }
    );
}

void prepare_vxgi_generate_mipmap_volume(
    ResRO<VxgiVolumes> volumes,
    ResRW<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRO<GraphicsDevice> device
) {
    if (generate_mipmap_volume->prepared_resolution ==
            volumes->config.voxel_resolution &&
        !generate_mipmap_volume->mip_entries.empty()) {
        return;
    }

    generate_mipmap_volume->mip_entries.clear();
    uint32 mip_dimension = volumes->config.voxel_resolution / 4;
    uint32 mip_level = 0;

    while (mip_dimension > 0) {
        std::array<std::shared_ptr<TextureView>, 6> dst_views;
        for (int i = 0; i < 6; ++i) {
            dst_views[i] = device->create_texture_view(
                TextureViewDescription {
                    .target = volumes->mipmap[i],
                    .base_mip_level = mip_level + 1,
                }
            );
        }

        auto resource_set = device->create_resource_set(
            ResourceSetDescription {
                .layout = generate_mipmap_volume->resource_layout,
                .resources = {
                    generate_mipmap_volume->uniform_buffer,
                    dst_views[0],
                    dst_views[1],
                    dst_views[2],
                    dst_views[3],
                    dst_views[4],
                    dst_views[5],
                    volumes->mipmap[0],
                    volumes->mipmap[1],
                    volumes->mipmap[2],
                    volumes->mipmap[3],
                    volumes->mipmap[4],
                    volumes->mipmap[5],
                },
            }
        );
        generate_mipmap_volume->mip_entries.push_back(
            VxgiGenerateMipmapVolume::MipEntry {
                .mip_dimension = mip_dimension,
                .mip_level = mip_level,
                .dst_views = std::move(dst_views),
                .resource_set = std::move(resource_set),
            }
        );

        mip_dimension /= 2;
        ++mip_level;
    }

    generate_mipmap_volume->prepared_resolution =
        volumes->config.voxel_resolution;
}

void generate_mipmap_volume(
    ResRO<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRO<GraphicsDevice> device
) {
    if (generate_mipmap_volume->mip_entries.empty()) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(generate_mipmap_volume->pipeline);

    for (const auto& entry : generate_mipmap_volume->mip_entries) {
        VxgiGenerateMipmapVolume::Uniform uniform {
            .mip_dimension = Vector3 {static_cast<float>(entry.mip_dimension)},
            .mip_level = static_cast<int>(entry.mip_level),
        };
        command_buffer->update_buffer(
            generate_mipmap_volume->uniform_buffer,
            &uniform,
            sizeof(VxgiGenerateMipmapVolume::Uniform)
        );
        command_buffer->set_resource_set(0, entry.resource_set);
        auto work_groups = (entry.mip_dimension + 7) / 8;
        command_buffer->dispatch(work_groups, work_groups, work_groups);
    }
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void setup_inject_radiance(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("shader://inject_radiance.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Compute},
            {
                uniform_buffer("VxgiInjectRadiance"),
                texture_read_only("shadow_map"),
                sampler("shadow_map_sampler"),
            }
        )
    );
    auto pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = shader_module,
            .resource_layouts = {
                volumes->resource_layout,
                voxelization->resource_layout,
                resource_layout,
            },
        }
    );
    auto uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiInjectRadianceUniform),
            .usages = BufferUsages::Uniform,
        }
    );

    commands.add_resource(
        VxgiInjectRadiance {
            .pipeline = pipeline,
            .uniform_buffer = uniform_buffer,
            .resource_layout = resource_layout,
            .resource_set = nullptr,
            .shadow_map_sampler = device->create_sampler(
                SamplerDescription {
                    .address_mode_u = SamplerAddressMode::ClampToEdge,
                    .address_mode_v = SamplerAddressMode::ClampToEdge,
                    .address_mode_w = SamplerAddressMode::ClampToEdge,
                }
            ),
        }
    );
}

void prepare_inject_radiance(
    Query<
        const DirectionalLight,
        const Transform3d,
        const ViewUniformBuffer,
        const ShadowMap> query_directional_lights,
    Query<const PointLight, const Transform3d> query_point_lights,
    ResRW<VxgiInjectRadiance> inject_radiance,
    ResRO<GraphicsDevice> device,
    ResRO<RenderingDefaults> rendering_defaults
) {
    VxgiInjectRadianceUniform uniform {};
    std::shared_ptr<Texture> shadow_map_texture;

    int dir_light_count = 0;
    for (const auto& [light, transform, view_uniform_buffer, shadow_map] :
         query_directional_lights) {
        if (dir_light_count >= 1) {
            break;
        }
        auto& dir_light = uniform.directional_lights[dir_light_count];
        dir_light.diffuse = light.color.to_vector3() * light.intensity;
        dir_light.position = transform.position;
        dir_light.direction = -transform.forward();
        dir_light.shadowing_method = 1;
        shadow_map_texture = shadow_map.texture;
        ++dir_light_count;
        uniform.light_view_projection =
            view_uniform_buffer.uniform.clip_from_world;
    }
    uniform.num_directional_lights = dir_light_count;

    int point_light_count = 0;
    for (const auto& [light, transform] : query_point_lights) {
        if (point_light_count >= 6) {
            break;
        }
        auto& point_light = uniform.point_lights[point_light_count];
        point_light.diffuse = light.color.to_vector3() * light.intensity;
        point_light.position = transform.position;
        point_light.shadowing_method = 2;
        ++point_light_count;
    }
    uniform.num_point_lights = point_light_count;

    device->update_buffer(
        inject_radiance->uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiInjectRadianceUniform)
    );

    auto selected_shadow_map = shadow_map_texture ?
                                   shadow_map_texture :
                                   rendering_defaults->default_texture;
    if (inject_radiance->resource_set_shadow_map != selected_shadow_map.get() ||
        !inject_radiance->resource_set) {
        inject_radiance->resource_set = device->create_resource_set(
            ResourceSetDescription {
                .layout = inject_radiance->resource_layout,
                .resources = {
                    inject_radiance->uniform_buffer,
                    selected_shadow_map,
                    inject_radiance->shadow_map_sampler,
                },
            }
        );
        inject_radiance->resource_set_shadow_map = selected_shadow_map.get();
    }
}

void inject_radiance(
    ResRW<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<VxgiInjectRadiance> inject_radiance,
    ResRO<VxgiGenerateMipmapBase> mipmap_base,
    ResRO<VxgiGenerateMipmapVolume> mipmap_volume,
    ResRO<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(inject_radiance->pipeline);
    command_buffer->set_resource_set(0, volumes->resource_set);
    command_buffer->set_resource_set(1, voxelization->resource_set);
    command_buffer->set_resource_set(2, inject_radiance->resource_set);
    auto work_groups = (volumes->config.voxel_resolution) / 8;
    command_buffer->dispatch(work_groups, work_groups, work_groups);
    command_buffer->end();
    device->submit_commands(command_buffer);

    generate_mipmap_base(volumes, mipmap_base, device);
    generate_mipmap_volume(mipmap_volume, device);
}

void setup_inject_propagation(
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("shader://inject_propagation.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Compute},
            {
                uniform_buffer("VxgiInjectPropagation"),
                texture_read_write("voxel_composite"),
                texture_read_only("voxel_albedo"),
                texture_read_only("voxel_normal"),
                texture_read_only("voxel_tex_mipmap[0]"),
                texture_read_only("voxel_tex_mipmap[1]"),
                texture_read_only("voxel_tex_mipmap[2]"),
                texture_read_only("voxel_tex_mipmap[3]"),
                texture_read_only("voxel_tex_mipmap[4]"),
                texture_read_only("voxel_tex_mipmap[5]"),
                sampler("voxel_sampler"),
            }
        )
    );
    auto pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = shader_module,
            .resource_layouts = {resource_layout},
        }
    );
    auto uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiInjectPropagationUniform),
            .usages = BufferUsages::Uniform,
        }
    );
    VxgiInjectPropagationUniform uniform {
        .volume_dimension = static_cast<int>(volumes->config.voxel_resolution),
    };
    device->update_buffer(
        uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiInjectPropagationUniform)
    );
    auto resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resource_layout,
            .resources = {
                uniform_buffer,
                volumes->radiance,
                volumes->albedo,
                volumes->normal,
                volumes->mipmap[0],
                volumes->mipmap[1],
                volumes->mipmap[2],
                volumes->mipmap[3],
                volumes->mipmap[4],
                volumes->mipmap[5],
                device->create_sampler(
                    SamplerDescription {
                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                        .address_mode_w = SamplerAddressMode::ClampToEdge,
                    }
                ),
            },
        }
    );

    commands.add_resource(
        VxgiInjectPropagation {
            .pipeline = pipeline,
            .resource_layout = resource_layout,
            .resource_set = resource_set,
            .uniform_buffer = uniform_buffer,
        }
    );
}

void inject_propagation(
    ResRW<VxgiVolumes> volumes,
    ResRO<VxgiInjectPropagation> inject_propagation,
    ResRO<VxgiGenerateMipmapBase> mipmap_base,
    ResRO<VxgiGenerateMipmapVolume> mipmap_volume,
    ResRO<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(inject_propagation->pipeline);
    command_buffer->set_resource_set(0, inject_propagation->resource_set);
    auto work_groups = (volumes->config.voxel_resolution) / 8;
    command_buffer->dispatch(work_groups, work_groups, work_groups);
    command_buffer->end();
    device->submit_commands(command_buffer);
    generate_mipmap_base(volumes, mipmap_base, device);
    generate_mipmap_volume(mipmap_volume, device);
}

void setup_vxgi_lighting(ResRO<GraphicsDevice> device, Commands commands) {
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {
                uniform_buffer("Vxgi"),
                texture_read_only("voxel_visibility"),
                texture_read_only("voxel_tex"),
                texture_read_only("voxel_tex_mipmap[0]"),
                texture_read_only("voxel_tex_mipmap[1]"),
                texture_read_only("voxel_tex_mipmap[2]"),
                texture_read_only("voxel_tex_mipmap[3]"),
                texture_read_only("voxel_tex_mipmap[4]"),
                texture_read_only("voxel_tex_mipmap[5]"),
                sampler("voxel_sampler"),
                texture_read_only("shadow_map"),
                sampler("shadow_map_sampler"),
            }
        )
    );
    auto uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiLightingUniform),
            .usages = BufferUsages::Uniform,
        }
    );

    commands.add_resource(
        VxgiLighting {
            .uniform_buffer = uniform_buffer,
            .resource_layout = resource_layout,
            .resource_set = nullptr,
            .voxel_sampler = device->create_sampler(
                SamplerDescription {
                    .address_mode_u = SamplerAddressMode::ClampToEdge,
                    .address_mode_v = SamplerAddressMode::ClampToEdge,
                    .address_mode_w = SamplerAddressMode::ClampToEdge,
                }
            ),
            .shadow_map_sampler = device->create_sampler(
                SamplerDescription {
                    .address_mode_u = SamplerAddressMode::ClampToEdge,
                    .address_mode_v = SamplerAddressMode::ClampToEdge,
                    .address_mode_w = SamplerAddressMode::ClampToEdge,
                }
            ),
        }
    );
}

void prepare_vxgi_lighting(
    Query<
        const DirectionalLight,
        const Transform3d,
        const ViewUniformBuffer,
        const ShadowMap> query_directional_lights,
    Query<const PointLight, const Transform3d> query_point_lights,
    ResRW<VxgiLighting> vxgi_lighting,
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<GraphicsDevice> device,
    ResRO<RenderingDefaults> rendering_defaults
) {
    VxgiLightingUniform uniform {};
    std::shared_ptr<Texture> shadow_map = nullptr;

    int dir_light_count = 0;
    for (const auto& [light, transform, view_uniform_buffer, _shadow_map] :
         query_directional_lights) {
        if (dir_light_count >= 1) {
            break;
        }
        auto& dir_light = uniform.directional_lights[dir_light_count];
        dir_light.diffuse = light.color.to_vector3() * light.intensity;
        dir_light.specular = dir_light.diffuse;
        dir_light.position = transform.position;
        dir_light.ambient = Vector3 {0.0f};
        dir_light.direction = -transform.forward();
        dir_light.shadowing_method = 1;
        uniform.light_view_projection =
            view_uniform_buffer.uniform.clip_from_world;
        shadow_map = _shadow_map.texture;
        ++dir_light_count;
    }
    uniform.num_directional_lights = dir_light_count;

    int point_light_count = 0;
    for (const auto& [light, transform] : query_point_lights) {
        if (point_light_count >= 6) {
            break;
        }
        auto& point_light = uniform.point_lights[point_light_count];
        point_light.diffuse = light.color.to_vector3() * light.intensity;
        point_light.specular = point_light.diffuse;
        point_light.position = transform.position;
        point_light.shadowing_method = 2;
        ++point_light_count;
    }
    uniform.num_point_lights = point_light_count;

    auto axis_size = voxelization->scene_aabb.extent() * 2.0f;
    auto center = voxelization->scene_aabb.center();
    auto volume_grid_size = std::max({axis_size.x, axis_size.y, axis_size.z});
    auto half_size = volume_grid_size * 0.5f;
    auto cube_half_extent = Vector3 {half_size, half_size, half_size};
    auto cube_min_point = center - cube_half_extent;
    auto cube_max_point = center + cube_half_extent;
    uniform.voxel_scale = 1.0f / volume_grid_size;
    uniform.world_min_point = cube_min_point;
    uniform.world_max_point = cube_max_point;
    uniform.volume_dimension =
        static_cast<int>(volumes->config.voxel_resolution);

    device->update_buffer(
        vxgi_lighting->uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiLightingUniform)
    );

    auto selected_shadow_map =
        shadow_map ? shadow_map : rendering_defaults->default_texture;
    if (vxgi_lighting->resource_set_shadow_map != selected_shadow_map.get() ||
        !vxgi_lighting->resource_set) {
        vxgi_lighting->resource_set = device->create_resource_set(
            ResourceSetDescription {
                .layout = vxgi_lighting->resource_layout,
                .resources = {
                    vxgi_lighting->uniform_buffer,
                    volumes->normal,
                    volumes->radiance,
                    volumes->mipmap[0],
                    volumes->mipmap[1],
                    volumes->mipmap[2],
                    volumes->mipmap[3],
                    volumes->mipmap[4],
                    volumes->mipmap[5],
                    vxgi_lighting->voxel_sampler,
                    selected_shadow_map,
                    vxgi_lighting->shadow_map_sampler,
                },
            }
        );
        vxgi_lighting->resource_set_shadow_map = selected_shadow_map.get();
    }
}

void VxgiPlugin::setup(App& app) {
    app.add_event<SceneSpawnedEvent>()
        .add_resource<VxgiVolumes>()
        .add_systems(
            StartUp,
            chain(
                setup_vxgi,
                all(setup_inject_radiance,
                    setup_inject_propagation,
                    setup_vxgi_generate_mipmap_base,
                    setup_vxgi_generate_mipmap_volume,
                    setup_vxgi_lighting)
            )
        )
        .add_systems(
            RenderUpdate,
            chain(
                compute_scene_aabb,
                prepare_vxgi_voxelization,
                prepare_vxgi_generate_mipmap_volume,
                prepare_inject_radiance,
                prepare_vxgi_lighting
            ) | after(setup_shadow_map) |
                in_set<RenderingSystems::PrepareResources>(),
            chain(
                mark_vxgi_voxelization_dirty,
                queue_vxgi_voxelization_pipelines
            ) | in_set<RenderingSystems::Queue>(),
            chain(voxelize_scene, inject_radiance, inject_propagation) |
                in_set<RenderingSystems::Render>()
        );
}

} // namespace fei
