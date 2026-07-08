#include "pbr/vxgi.hpp"

#include "base/hash.hpp"
#include "scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <utility>

namespace fei {

namespace {

struct VxgiVoxelDrawItem {
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    uint32 index_count {};
    uint32 vertex_count {};
    std::shared_ptr<const ResourceSet> mesh_set;
    std::shared_ptr<const ResourceSet> material_set;
    std::shared_ptr<Pipeline> pipeline;
};

struct VxgiVoxelizationPassData {
    std::vector<VxgiVoxelDrawItem> draw_items;
    RgResourceSetHandle volumes_set;
    RgResourceSetHandle voxelization_set;
    RgResourceSetHandle accumulation_set;
    RgTextureHandle target;
    uint32 viewport_size {};
};

struct VxgiClearPassData {
    RgResourceSetHandle volumes_set;
    RgResourceSetHandle voxelization_set;
    RgResourceSetHandle accumulation_set;
    std::shared_ptr<Pipeline> pipeline;
    uint32 work_groups {};
};

struct VxgiResolveVoxelsPassData {
    RgResourceSetHandle volumes_set;
    RgResourceSetHandle voxelization_set;
    RgResourceSetHandle accumulation_set;
    std::shared_ptr<Pipeline> pipeline;
    uint32 work_groups {};
};

struct VxgiInjectRadiancePassData {
    RgResourceSetHandle volumes_set;
    RgResourceSetHandle voxelization_set;
    RgResourceSetHandle lighting_set;
    RgResourceSetHandle inject_set;
    std::shared_ptr<Pipeline> pipeline;
    uint32 work_groups {};
};

struct VxgiMipmapBasePassData {
    RgResourceSetHandle resource_set;
    std::shared_ptr<Pipeline> pipeline;
    uint32 work_groups {};
};

struct VxgiMipmapVolumeEntryPassData {
    RgResourceSetHandle resource_set;
    uint32 mip_dimension {};
    uint32 mip_level {};
    uint32 work_groups {};
};

struct VxgiMipmapVolumePassData {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<Buffer> uniform_buffer;
    std::vector<VxgiMipmapVolumeEntryPassData> entries;
};

struct VxgiInjectPropagationPassData {
    RgResourceSetHandle resource_set;
    std::shared_ptr<Pipeline> pipeline;
    uint32 work_groups {};
};

TextureDescription texture_desc_from(const Texture& texture) {
    return TextureDescription {
        .width = texture.width(),
        .height = texture.height(),
        .depth = texture.depth(),
        .mip_level = texture.mip_level(),
        .layer = texture.layer(),
        .texture_format = texture.format(),
        .texture_usage = texture.usage(),
        .texture_type = texture.type(),
        .sample_count = texture.sample_count(),
    };
}

std::size_t vxgi_voxel_count(uint32 voxel_resolution) {
    const auto resolution = static_cast<std::size_t>(voxel_resolution);
    return resolution * resolution * resolution;
}

std::shared_ptr<Buffer> create_vxgi_accumulation_buffer(
    const GraphicsDevice& device,
    uint32 voxel_resolution,
    std::size_t channels
) {
    return device.create_buffer(
        BufferDescription {
            .size =
                vxgi_voxel_count(voxel_resolution) * channels * sizeof(uint32),
            .usages = BufferUsages::Storage,
        }
    );
}

OutputDescription single_color_output_description(PixelFormat format) {
    return OutputDescription {
        .color_attachments =
            {
                OutputAttachmentDescription {
                    .format = format,
                },
            },
        .sample_count = TextureSampleCount::Count1,
    };
}

std::vector<RenderGraphResourceBinding>
vxgi_volume_bindings(const VxgiGraphHandles& handles) {
    return {
        handles.albedo,
        handles.normal,
        handles.emissive,
        handles.radiance,
        handles.static_flag,
    };
}

std::vector<RenderGraphResourceBinding>
vxgi_voxelization_bindings(const VxgiVoxelization& voxelization) {
    return {
        voxelization.voxelization_uniform_buffer,
    };
}

std::vector<RenderGraphResourceBinding>
vxgi_accumulation_bindings(const VxgiVoxelization& voxelization) {
    return {
        voxelization.albedo_accumulation_buffer,
        voxelization.normal_accumulation_buffer,
        voxelization.emissive_accumulation_buffer,
        voxelization.count_accumulation_buffer,
    };
}

VxgiGraphHandles& ensure_vxgi_graph_handles(
    RenderGraphBuilder& builder,
    RenderGraphBlackboard& blackboard,
    const VxgiVolumes& volumes
) {
    auto& handles = blackboard.contains<VxgiGraphHandles>() ?
                        blackboard.get<VxgiGraphHandles>() :
                        blackboard.emplace<VxgiGraphHandles>();
    if (handles.imported) {
        return handles;
    }

    handles.albedo = builder.import_texture("vxgi.albedo", volumes.albedo);
    handles.normal = builder.import_texture("vxgi.normal", volumes.normal);
    handles.emissive =
        builder.import_texture("vxgi.emissive", volumes.emissive);
    handles.radiance =
        builder.import_texture("vxgi.radiance", volumes.radiance);
    handles.static_flag =
        builder.import_texture("vxgi.static_flag", volumes.static_flag);
    for (std::size_t i = 0; i < handles.mipmap.size(); ++i) {
        handles.mipmap[i] = builder.import_texture(
            std::format("vxgi.mipmap.{}", i),
            volumes.mipmap[i]
        );
    }
    handles.imported = true;
    return handles;
}

RgResourceSetHandle create_vxgi_volumes_set(
    RenderGraphBuilder& builder,
    const VxgiVolumes& volumes,
    const VxgiGraphHandles& handles
) {
    return builder.create_resource_set(
        "vxgi.volumes",
        volumes.resource_layout,
        vxgi_volume_bindings(handles)
    );
}

RgTextureHandle shadow_map_handle(RenderGraphBlackboard& blackboard) {
    if (!blackboard.contains<ShadowMapGraphHandles>()) {
        return {};
    }
    return blackboard.get<ShadowMapGraphHandles>().first();
}

std::vector<RenderGraphResourceBinding> vxgi_mipmap_base_bindings(
    const VxgiGenerateMipmapBase& mipmap_base,
    const VxgiGraphHandles& handles
) {
    return {
        mipmap_base.uniform_buffer,
        handles.radiance,
        handles.mipmap[0],
        handles.mipmap[1],
        handles.mipmap[2],
        handles.mipmap[3],
        handles.mipmap[4],
        handles.mipmap[5],
    };
}

std::vector<RenderGraphResourceBinding> vxgi_mipmap_volume_bindings(
    const VxgiGenerateMipmapVolume& mipmap_volume,
    const VxgiGenerateMipmapVolume::MipEntry& entry,
    const VxgiGraphHandles& handles
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

        std::shared_ptr<const Buffer> index_buffer;
        if (auto mesh_index_buffer = gpu_mesh.index_buffer()) {
            index_buffer = *mesh_index_buffer;
        }

        draw_items.push_back(
            VxgiVoxelDrawItem {
                .vertex_buffer = gpu_mesh.vertex_buffer(),
                .index_buffer = std::move(index_buffer),
                .index_count = static_cast<uint32>(
                    gpu_mesh.index_buffer_size() / sizeof(std::uint32_t)
                ),
                .vertex_count = static_cast<uint32>(gpu_mesh.vertex_count()),
                .mesh_set = mesh_uniform_it->second.resource_set,
                .material_set = material.resource_set(),
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
    std::shared_ptr<const ResourceLayout> voxelization_layout,
    std::shared_ptr<const ResourceLayout> accumulation_layout
) :
    m_shader_modules(std::move(shader_modules)),
    m_volumes_layout(std::move(volumes_layout)),
    m_voxelization_layout(std::move(voxelization_layout)),
    m_accumulation_layout(std::move(accumulation_layout)) {
    for (const auto& shader_module : m_shader_modules) {
        hash_combine(m_cache_key, shader_module.get());
    }
    hash_combine(m_cache_key, m_volumes_layout.get());
    hash_combine(m_cache_key, m_voxelization_layout.get());
    hash_combine(m_cache_key, m_accumulation_layout.get());
}

void VxgiVoxelizationSpecializer::specialize(
    RenderPipelineDescription& desc,
    const GpuMesh& /*mesh*/,
    const PreparedMaterial& /*material*/
) const {
    desc.shader_program.shaders = m_shader_modules;
    remove_vertex_input_attribute(desc, Mesh::ATTRIBUTE_TANGENT.id);
    desc.depth_stencil_state = DepthStencilStateDescription::Disabled;
    desc.rasterizer_state.cull_mode = CullMode::None;
    desc.resource_layouts.push_back(m_volumes_layout);
    desc.resource_layouts.push_back(m_voxelization_layout);
    desc.resource_layouts.push_back(m_accumulation_layout);
    desc.output_description =
        single_color_output_description(PixelFormat::Rgba8Unorm);
}

void setup_vxgi(
    ResRW<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
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
    auto static_flag_desc = desc;
    static_flag_desc.texture_format = PixelFormat::R8Unorm;
    volumes->static_flag = device->create_texture(static_flag_desc);

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
             ShaderStages::Fragment,
             ShaderStages::Compute},
            {
                texture_read_write("voxel_albedo"),
                texture_read_write("voxel_normal"),
                texture_read_write("voxel_emissive"),
                texture_read_write("voxel_radiance"),
                texture_read_write("static_voxel_flag"),
            }
        )
    );

    std::vector<std::shared_ptr<const ShaderModule>> shader_modules {
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/voxelization.slang"),
            ShaderStages::Vertex,
            {}
        ),
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/voxelization.slang"),
            ShaderStages::Geometry,
            {}
        ),
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/voxelization.slang"),
            ShaderStages::Fragment,
            {}
        ),
    };

    auto voxelization_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex,
             ShaderStages::Geometry,
             ShaderStages::Fragment,
             ShaderStages::Compute},
            {uniform_buffer("VxgiVoxelization")}
        )
    );
    auto accumulation_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment, ShaderStages::Compute},
            {
                storage_buffer_read_write("voxel_albedo_accum"),
                storage_buffer_read_write("voxel_normal_accum"),
                storage_buffer_read_write("voxel_emissive_accum"),
                storage_buffer_read_write("voxel_count_accum"),
            }
        )
    );

    auto voxelization_uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiVoxelizationUniform),
            .usages = BufferUsages::Uniform,
        }
    );
    auto albedo_accumulation_buffer =
        create_vxgi_accumulation_buffer(*device, config.voxel_resolution, 4);
    auto normal_accumulation_buffer =
        create_vxgi_accumulation_buffer(*device, config.voxel_resolution, 3);
    auto emissive_accumulation_buffer =
        create_vxgi_accumulation_buffer(*device, config.voxel_resolution, 4);
    auto count_accumulation_buffer =
        create_vxgi_accumulation_buffer(*device, config.voxel_resolution, 1);
    auto clear_shader_module = shader_cache->get_or_compile(
        AssetPath("shader://pbr/clear_voxels.slang"),
        ShaderStages::Compute,
        {}
    );
    auto clear_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = clear_shader_module,
            .resource_layouts = {
                volumes->resource_layout,
                voxelization_resource_layout,
                accumulation_resource_layout
            },
        }
    );
    auto resolve_shader_module = shader_cache->get_or_compile(
        AssetPath("shader://pbr/resolve_voxels.slang"),
        ShaderStages::Compute,
        {}
    );
    auto resolve_pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = resolve_shader_module,
            .resource_layouts = {
                volumes->resource_layout,
                voxelization_resource_layout,
                accumulation_resource_layout
            },
        }
    );

    commands.add_resource(
        VxgiVoxelization {
            .voxelization_uniform_buffer = voxelization_uniform_buffer,
            .albedo_accumulation_buffer = albedo_accumulation_buffer,
            .normal_accumulation_buffer = normal_accumulation_buffer,
            .emissive_accumulation_buffer = emissive_accumulation_buffer,
            .count_accumulation_buffer = count_accumulation_buffer,
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
            .accumulation_layout = accumulation_resource_layout,
            .clear_pipeline = clear_pipeline,
            .resolve_pipeline = resolve_pipeline,
            .pipeline_specializer = VxgiVoxelizationSpecializer(
                shader_modules,
                volumes->resource_layout,
                voxelization_resource_layout,
                accumulation_resource_layout
            ),
        }
    );
}

void compute_scene_aabb(
    ResRW<VxgiVoxelization> voxelization,
    Query<const Transform3d, const Aabb>::Filter<With<Mesh3d>> query
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
    for (const auto& [transform3d, aabb] : query) {
        auto world_aabb = transform_aabb(aabb, transform3d.to_matrix());
        auto min = world_aabb.min;
        auto max = world_aabb.max;
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
        proj *
        look_at(center + Vector3::Up * half_size, center, Vector3::Forward);
    uniform.view_projections[2] =
        proj *
        look_at(center + Vector3::Forward * half_size, center, Vector3::Up);
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

void add_vxgi_clear_pass(
    RenderGraph& render_graph,
    const VxgiVolumes& volumes,
    const VxgiVoxelization& voxelization
) {
    render_graph.add_pass<VxgiClearPassData>(
        "vxgi_clear",
        [&](RenderGraphBuilder& builder, VxgiClearPassData& data) {
            auto& handles = ensure_vxgi_graph_handles(
                builder,
                render_graph.blackboard(),
                volumes
            );
            data.volumes_set =
                create_vxgi_volumes_set(builder, volumes, handles);
            data.voxelization_set = builder.create_resource_set(
                "vxgi.voxelization",
                voxelization.resource_layout,
                vxgi_voxelization_bindings(voxelization)
            );
            data.accumulation_set = builder.create_resource_set(
                "vxgi.accumulation",
                voxelization.accumulation_layout,
                vxgi_accumulation_bindings(voxelization)
            );
            data.pipeline = voxelization.clear_pipeline;
            data.work_groups = (volumes.config.voxel_resolution + 7) / 8;

            handles.albedo = builder.write_texture(
                handles.albedo,
                RenderGraphAccess::TextureReadWrite
            );
            handles.normal = builder.write_texture(
                handles.normal,
                RenderGraphAccess::TextureReadWrite
            );
            handles.emissive = builder.write_texture(
                handles.emissive,
                RenderGraphAccess::TextureReadWrite
            );
            handles.radiance = builder.write_texture(
                handles.radiance,
                RenderGraphAccess::TextureReadWrite
            );
            handles.static_flag = builder.write_texture(
                handles.static_flag,
                RenderGraphAccess::TextureReadWrite
            );
        },
        [](RenderGraphContext& context, const VxgiClearPassData& data) {
            auto& command_buffer = context.command_buffer();
            command_buffer.set_compute_pipeline(data.pipeline);
            command_buffer.set_resource_set(
                0,
                context.resource_set(data.volumes_set)
            );
            command_buffer.set_resource_set(
                1,
                context.resource_set(data.voxelization_set)
            );
            command_buffer.set_resource_set(
                2,
                context.resource_set(data.accumulation_set)
            );
            command_buffer
                .dispatch(data.work_groups, data.work_groups, data.work_groups);
        }
    );
}

void add_vxgi_resolve_voxels_pass(
    RenderGraph& render_graph,
    const VxgiVolumes& volumes,
    const VxgiVoxelization& voxelization
) {
    render_graph.add_pass<VxgiResolveVoxelsPassData>(
        "vxgi_resolve_voxels",
        [&](RenderGraphBuilder& builder, VxgiResolveVoxelsPassData& data) {
            auto& handles = ensure_vxgi_graph_handles(
                builder,
                render_graph.blackboard(),
                volumes
            );
            data.volumes_set =
                create_vxgi_volumes_set(builder, volumes, handles);
            data.voxelization_set = builder.create_resource_set(
                "vxgi.voxelization",
                voxelization.resource_layout,
                vxgi_voxelization_bindings(voxelization)
            );
            data.accumulation_set = builder.create_resource_set(
                "vxgi.accumulation",
                voxelization.accumulation_layout,
                vxgi_accumulation_bindings(voxelization)
            );
            data.pipeline = voxelization.resolve_pipeline;
            data.work_groups = (volumes.config.voxel_resolution + 7) / 8;

            handles.albedo = builder.write_texture(
                handles.albedo,
                RenderGraphAccess::TextureReadWrite
            );
            handles.normal = builder.write_texture(
                handles.normal,
                RenderGraphAccess::TextureReadWrite
            );
            handles.emissive = builder.write_texture(
                handles.emissive,
                RenderGraphAccess::TextureReadWrite
            );
            handles.static_flag = builder.write_texture(
                handles.static_flag,
                RenderGraphAccess::TextureReadWrite
            );
        },
        [](RenderGraphContext& context, const VxgiResolveVoxelsPassData& data) {
            auto& command_buffer = context.command_buffer();
            command_buffer.set_compute_pipeline(data.pipeline);
            command_buffer.set_resource_set(
                0,
                context.resource_set(data.volumes_set)
            );
            command_buffer.set_resource_set(
                1,
                context.resource_set(data.voxelization_set)
            );
            command_buffer.set_resource_set(
                2,
                context.resource_set(data.accumulation_set)
            );
            command_buffer
                .dispatch(data.work_groups, data.work_groups, data.work_groups);
        }
    );
}

void build_vxgi_voxelization_pass(
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
    ResRW<RenderGraph> render_graph
) {
    if (!voxelization->dirty) {
        return;
    }

    std::vector<VxgiVoxelDrawItem> draw_items;
    if (!collect_vxgi_voxel_draw_items(
            query_meshes,
            *voxelization,
            *pipelines,
            *pipeline_cache,
            *gpu_meshes,
            *materials,
            *mesh_uniforms,
            draw_items
        )) {
        return;
    }

    add_vxgi_clear_pass(*render_graph, *volumes, *voxelization);
    if (draw_items.empty()) {
        voxelization->dirty = false;
        return;
    }

    render_graph->add_pass<VxgiVoxelizationPassData>(
        "vxgi_voxelize",
        [&](RenderGraphBuilder& builder, VxgiVoxelizationPassData& data) {
            auto& handles = ensure_vxgi_graph_handles(
                builder,
                render_graph->blackboard(),
                *volumes
            );
            data.draw_items = std::move(draw_items);
            data.volumes_set =
                create_vxgi_volumes_set(builder, *volumes, handles);
            data.voxelization_set = builder.create_resource_set(
                "vxgi.voxelization",
                voxelization->resource_layout,
                vxgi_voxelization_bindings(*voxelization)
            );
            data.accumulation_set = builder.create_resource_set(
                "vxgi.accumulation",
                voxelization->accumulation_layout,
                vxgi_accumulation_bindings(*voxelization)
            );
            data.target = builder.create_texture(
                "vxgi.voxelization_target",
                texture_desc_from(*voxelization->temp_texture)
            );
            data.target = builder.write_texture(
                data.target,
                RenderGraphAccess::ColorAttachmentWrite
            );
            data.viewport_size = volumes->config.voxel_resolution;

            handles.albedo = builder.write_texture(
                handles.albedo,
                RenderGraphAccess::TextureReadWrite
            );
            handles.normal = builder.write_texture(
                handles.normal,
                RenderGraphAccess::TextureReadWrite
            );
            handles.emissive = builder.write_texture(
                handles.emissive,
                RenderGraphAccess::TextureReadWrite
            );
            handles.static_flag = builder.write_texture(
                handles.static_flag,
                RenderGraphAccess::TextureReadWrite
            );
        },
        [](RenderGraphContext& context, const VxgiVoxelizationPassData& data) {
            auto& command_buffer = context.command_buffer();
            command_buffer.begin_render_pass(
                RenderPassDescription {
                    .color_attachments = {
                        RenderPassColorAttachment {
                            .texture = context.texture(data.target),
                            .load_op = LoadOp::Clear,
                            .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                        },
                    },
                }
            );
            command_buffer
                .set_viewport(0, 0, data.viewport_size, data.viewport_size);
            auto volumes_set = context.resource_set(data.volumes_set);
            for (const auto& item : data.draw_items) {
                command_buffer.set_render_pipeline(item.pipeline);
                command_buffer.set_resource_set(1, item.mesh_set);
                command_buffer.set_resource_set(2, item.material_set);
                command_buffer.set_resource_set(3, volumes_set);
                command_buffer.set_resource_set(
                    4,
                    context.resource_set(data.voxelization_set)
                );
                command_buffer.set_resource_set(
                    5,
                    context.resource_set(data.accumulation_set)
                );
                command_buffer.set_vertex_buffer(item.vertex_buffer);
                if (item.index_buffer) {
                    command_buffer.set_index_buffer(
                        item.index_buffer,
                        IndexFormat::Uint32
                    );
                    command_buffer.draw_indexed(item.index_count);
                } else {
                    command_buffer.draw(0, item.vertex_count);
                }
            }
            command_buffer.end_render_pass();
        }
    );

    add_vxgi_resolve_voxels_pass(*render_graph, *volumes, *voxelization);
    voxelization->dirty = false;
}

void setup_vxgi_generate_mipmap_base(
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
) {
    auto shader_module = shader_cache->get_or_compile(
        AssetPath("shader://pbr/aniso_mipmapbase.slang"),
        ShaderStages::Compute,
        {}
    );
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
    commands.add_resource(
        VxgiGenerateMipmapBase {
            .pipeline = pipeline,
            .resource_layout = resource_layout,
            .uniform_buffer = uniform_buffer,
        }
    );
}

void add_vxgi_mipmap_base_pass(
    RenderGraph& render_graph,
    const VxgiVolumes& volumes,
    const VxgiGenerateMipmapBase& generate_mipmap_base,
    std::string name
) {
    render_graph.add_pass<VxgiMipmapBasePassData>(
        std::move(name),
        [&](RenderGraphBuilder& builder, VxgiMipmapBasePassData& data) {
            auto& handles = ensure_vxgi_graph_handles(
                builder,
                render_graph.blackboard(),
                volumes
            );
            data.pipeline = generate_mipmap_base.pipeline;
            data.resource_set = builder.create_resource_set(
                "vxgi.mipmap_base",
                generate_mipmap_base.resource_layout,
                vxgi_mipmap_base_bindings(generate_mipmap_base, handles)
            );
            data.work_groups = (volumes.config.voxel_resolution / 2) / 8;
            for (auto& mipmap : handles.mipmap) {
                mipmap = builder.write_texture(
                    mipmap,
                    RenderGraphAccess::TextureReadWrite
                );
            }
        },
        [](RenderGraphContext& context, const VxgiMipmapBasePassData& data) {
            auto& command_buffer = context.command_buffer();
            command_buffer.set_compute_pipeline(data.pipeline);
            command_buffer.set_resource_set(
                0,
                context.resource_set(data.resource_set)
            );
            command_buffer
                .dispatch(data.work_groups, data.work_groups, data.work_groups);
        }
    );
}

void build_vxgi_mipmap_base_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapBase> generate_mipmap_base,
    ResRW<RenderGraph> render_graph
) {
    add_vxgi_mipmap_base_pass(
        *render_graph,
        *volumes,
        *generate_mipmap_base,
        "vxgi_mipmap_base"
    );
}

void build_vxgi_mipmap_base_after_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapBase> generate_mipmap_base,
    ResRW<RenderGraph> render_graph
) {
    add_vxgi_mipmap_base_pass(
        *render_graph,
        *volumes,
        *generate_mipmap_base,
        "vxgi_mipmap_base_after_propagation"
    );
}

void setup_vxgi_generate_mipmap_volume(
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
) {
    auto shader_module = shader_cache->get_or_compile(
        AssetPath("shader://pbr/aniso_mipmapvolume.slang"),
        ShaderStages::Compute,
        {}
    );
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
        std::array<std::shared_ptr<TextureView>, 6> src_views;
        std::array<std::shared_ptr<TextureView>, 6> dst_views;
        for (int i = 0; i < 6; ++i) {
            src_views[i] = device->create_texture_view(
                TextureViewDescription {
                    .target = volumes->mipmap[i],
                    .base_mip_level = mip_level,
                }
            );
            dst_views[i] = device->create_texture_view(
                TextureViewDescription {
                    .target = volumes->mipmap[i],
                    .base_mip_level = mip_level + 1,
                }
            );
        }

        generate_mipmap_volume->mip_entries.push_back(
            VxgiGenerateMipmapVolume::MipEntry {
                .mip_dimension = mip_dimension,
                .mip_level = mip_level,
                .src_views = std::move(src_views),
                .dst_views = std::move(dst_views),
            }
        );

        mip_dimension /= 2;
        ++mip_level;
    }

    generate_mipmap_volume->prepared_resolution =
        volumes->config.voxel_resolution;
}

void add_vxgi_mipmap_volume_pass(
    RenderGraph& render_graph,
    const VxgiVolumes& volumes,
    const VxgiGenerateMipmapVolume& generate_mipmap_volume,
    std::string name
) {
    if (generate_mipmap_volume.mip_entries.empty()) {
        return;
    }

    render_graph.add_pass<VxgiMipmapVolumePassData>(
        std::move(name),
        [&](RenderGraphBuilder& builder, VxgiMipmapVolumePassData& data) {
            auto& handles = ensure_vxgi_graph_handles(
                builder,
                render_graph.blackboard(),
                volumes
            );
            data.pipeline = generate_mipmap_volume.pipeline;
            data.uniform_buffer = generate_mipmap_volume.uniform_buffer;
            data.entries.clear();
            data.entries.reserve(generate_mipmap_volume.mip_entries.size());
            for (const auto& mipmap : handles.mipmap) {
                builder.read_texture(mipmap, RenderGraphAccess::TextureRead);
            }
            for (const auto& entry : generate_mipmap_volume.mip_entries) {
                auto resource_set = builder.create_resource_set(
                    "vxgi.mipmap_volume",
                    generate_mipmap_volume.resource_layout,
                    vxgi_mipmap_volume_bindings(
                        generate_mipmap_volume,
                        entry,
                        handles
                    )
                );
                data.entries.push_back(
                    VxgiMipmapVolumeEntryPassData {
                        .resource_set = resource_set,
                        .mip_dimension = entry.mip_dimension,
                        .mip_level = entry.mip_level,
                        .work_groups = (entry.mip_dimension + 7) / 8,
                    }
                );
            }
            for (auto& mipmap : handles.mipmap) {
                mipmap = builder.write_texture(
                    mipmap,
                    RenderGraphAccess::TextureReadWrite
                );
            }
        },
        [](RenderGraphContext& context, const VxgiMipmapVolumePassData& data) {
            auto& command_buffer = context.command_buffer();
            command_buffer.set_compute_pipeline(data.pipeline);

            for (const auto& entry : data.entries) {
                VxgiGenerateMipmapVolume::Uniform uniform {
                    .mip_dimension =
                        Vector3 {static_cast<float>(entry.mip_dimension)},
                    .mip_level = static_cast<int>(entry.mip_level),
                };
                command_buffer.update_buffer(
                    data.uniform_buffer,
                    &uniform,
                    sizeof(VxgiGenerateMipmapVolume::Uniform)
                );
                command_buffer.set_resource_set(
                    0,
                    context.resource_set(entry.resource_set)
                );
                command_buffer.dispatch(
                    entry.work_groups,
                    entry.work_groups,
                    entry.work_groups
                );
            }
        }
    );
}

void build_vxgi_mipmap_volume_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRW<RenderGraph> render_graph
) {
    add_vxgi_mipmap_volume_pass(
        *render_graph,
        *volumes,
        *generate_mipmap_volume,
        "vxgi_mipmap_volume"
    );
}

void build_vxgi_mipmap_volume_after_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRW<RenderGraph> render_graph
) {
    add_vxgi_mipmap_volume_pass(
        *render_graph,
        *volumes,
        *generate_mipmap_volume,
        "vxgi_mipmap_volume_after_propagation"
    );
}

void setup_inject_radiance(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<LightingResources> lighting,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
) {
    auto shader_module = shader_cache->get_or_compile(
        AssetPath("shader://pbr/inject_radiance.slang"),
        ShaderStages::Compute,
        {}
    );
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Compute},
            {
                uniform_buffer("VxgiInjectRadiance"),
            }
        )
    );
    auto pipeline = device->create_compute_pipeline(
        ComputePipelineDescription {
            .shader = shader_module,
            .resource_layouts = {
                volumes->resource_layout,
                voxelization->resource_layout,
                lighting->resource_layout,
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
        }
    );
}

void prepare_inject_radiance(
    ResRW<VxgiInjectRadiance> inject_radiance,
    ResRO<GraphicsDevice> device
) {
    VxgiInjectRadianceUniform uniform {};

    device->update_buffer(
        inject_radiance->uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiInjectRadianceUniform)
    );
}

void build_vxgi_inject_radiance_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<VxgiInjectRadiance> inject_radiance,
    ResRO<LightingResources> lighting,
    ResRO<RenderingDefaults> rendering_defaults,
    ResRW<RenderGraph> render_graph
) {
    render_graph->add_pass<VxgiInjectRadiancePassData>(
        "vxgi_inject_radiance",
        [&](RenderGraphBuilder& builder, VxgiInjectRadiancePassData& data) {
            auto& handles = ensure_vxgi_graph_handles(
                builder,
                render_graph->blackboard(),
                *volumes
            );
            data.volumes_set =
                create_vxgi_volumes_set(builder, *volumes, handles);
            data.voxelization_set = builder.create_resource_set(
                "vxgi.voxelization",
                voxelization->resource_layout,
                vxgi_voxelization_bindings(*voxelization)
            );
            data.lighting_set = builder.create_resource_set(
                "lighting",
                lighting->resource_layout,
                lighting_resource_bindings(
                    *lighting,
                    shadow_map_handle(render_graph->blackboard()),
                    rendering_defaults->default_texture
                )
            );
            data.inject_set = builder.create_resource_set(
                "vxgi.inject_radiance",
                inject_radiance->resource_layout,
                {inject_radiance->uniform_buffer}
            );
            data.pipeline = inject_radiance->pipeline;
            data.work_groups = volumes->config.voxel_resolution / 8;

            handles.normal = builder.write_texture(
                handles.normal,
                RenderGraphAccess::TextureReadWrite
            );
            handles.radiance = builder.write_texture(
                handles.radiance,
                RenderGraphAccess::TextureReadWrite
            );
        },
        [](RenderGraphContext& context,
           const VxgiInjectRadiancePassData& data) {
            auto& command_buffer = context.command_buffer();
            command_buffer.set_compute_pipeline(data.pipeline);
            command_buffer.set_resource_set(
                0,
                context.resource_set(data.volumes_set)
            );
            command_buffer.set_resource_set(
                1,
                context.resource_set(data.voxelization_set)
            );
            command_buffer.set_resource_set(
                2,
                context.resource_set(data.lighting_set)
            );
            command_buffer.set_resource_set(
                3,
                context.resource_set(data.inject_set)
            );
            command_buffer
                .dispatch(data.work_groups, data.work_groups, data.work_groups);
        }
    );
}

void setup_inject_propagation(
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
) {
    auto shader_module = shader_cache->get_or_compile(
        AssetPath("shader://pbr/inject_propagation.slang"),
        ShaderStages::Compute,
        {}
    );
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
    auto voxel_sampler = device->create_sampler(
        SamplerDescription {
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge,
        }
    );

    commands.add_resource(
        VxgiInjectPropagation {
            .pipeline = pipeline,
            .resource_layout = resource_layout,
            .uniform_buffer = uniform_buffer,
            .voxel_sampler = voxel_sampler,
        }
    );
}

void build_vxgi_inject_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiInjectPropagation> inject_propagation,
    ResRW<RenderGraph> render_graph
) {
    render_graph->add_pass<VxgiInjectPropagationPassData>(
        "vxgi_inject_propagation",
        [&](RenderGraphBuilder& builder, VxgiInjectPropagationPassData& data) {
            auto& handles = ensure_vxgi_graph_handles(
                builder,
                render_graph->blackboard(),
                *volumes
            );
            data.resource_set = builder.create_resource_set(
                "vxgi.inject_propagation",
                inject_propagation->resource_layout,
                vxgi_inject_propagation_resource_bindings(
                    *inject_propagation,
                    handles
                )
            );
            handles.radiance = builder.write_texture(
                handles.radiance,
                RenderGraphAccess::TextureReadWrite
            );
            data.pipeline = inject_propagation->pipeline;
            data.work_groups = volumes->config.voxel_resolution / 8;
        },
        [](RenderGraphContext& context,
           const VxgiInjectPropagationPassData& data) {
            auto& command_buffer = context.command_buffer();
            command_buffer.set_compute_pipeline(data.pipeline);
            command_buffer.set_resource_set(
                0,
                context.resource_set(data.resource_set)
            );
            command_buffer
                .dispatch(data.work_groups, data.work_groups, data.work_groups);
        }
    );
}

void setup_vxgi_resources(ResRO<GraphicsDevice> device, Commands commands) {
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
            }
        )
    );
    auto uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(VxgiUniform),
            .usages = BufferUsages::Uniform,
        }
    );

    commands.add_resource(
        VxgiResources {
            .uniform_buffer = uniform_buffer,
            .resource_layout = resource_layout,
            .voxel_sampler = device->create_sampler(
                SamplerDescription {
                    .address_mode_u = SamplerAddressMode::ClampToEdge,
                    .address_mode_v = SamplerAddressMode::ClampToEdge,
                    .address_mode_w = SamplerAddressMode::ClampToEdge,
                }
            ),
        }
    );
}

void prepare_vxgi_resources(
    ResRW<VxgiResources> vxgi,
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<GraphicsDevice> device
) {
    VxgiUniform uniform {};
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

    device
        ->update_buffer(vxgi->uniform_buffer, 0, &uniform, sizeof(VxgiUniform));
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
                    setup_vxgi_resources)
            ) | after(setup_lighting)
        )
        .add_systems(
            RenderUpdate,
            chain(
                compute_scene_aabb,
                prepare_vxgi_voxelization,
                prepare_vxgi_generate_mipmap_volume,
                prepare_inject_radiance,
                prepare_vxgi_resources
            ) | after(setup_shadow_map) |
                in_set<RenderingSystems::PrepareResources>(),
            chain(
                mark_vxgi_voxelization_dirty,
                queue_vxgi_voxelization_pipelines
            ) | in_set<RenderingSystems::Queue>(),
            chain(
                build_vxgi_voxelization_pass,
                build_vxgi_inject_radiance_pass,
                build_vxgi_mipmap_base_pass,
                build_vxgi_mipmap_volume_pass,
                build_vxgi_inject_propagation_pass,
                build_vxgi_mipmap_base_after_propagation_pass,
                build_vxgi_mipmap_volume_after_propagation_pass
            ) | in_set<RenderingSystems::BuildRenderGraph>()
        );
}

} // namespace fei
