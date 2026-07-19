#include "pbr/vxgi.hpp"

#include "base/hash.hpp"
#include "pbr/plugin.hpp"
#include "scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <utility>

namespace fei {

namespace {

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
    ResRO<RenderQueue> render_queue
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
    render_queue->write_buffer(
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
    std::array<std::shared_ptr<TextureView>, 6> output_views;
    for (int i = 0; i < 6; ++i) {
        output_views[i] = device->create_texture_view(
            TextureViewDescription {
                .target = volumes->mipmap[i],
                .base_mip_level = 0,
                .mip_levels = 1,
            }
        );
    }
    commands.add_resource(
        VxgiGenerateMipmapBase {
            .pipeline = pipeline,
            .resource_layout = resource_layout,
            .uniform_buffer = uniform_buffer,
            .output_views = std::move(output_views),
        }
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
    ResRO<RenderQueue> render_queue
) {
    VxgiInjectRadianceUniform uniform {};

    render_queue->write_buffer(
        inject_radiance->uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiInjectRadianceUniform)
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
    ResRO<RenderQueue> render_queue
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
    uniform.bounce_strength = volumes->config.bounce_strength;
    uniform.skylight_leaking = volumes->config.skylight_leaking;

    render_queue
        ->write_buffer(vxgi->uniform_buffer, 0, &uniform, sizeof(VxgiUniform));
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
            ) | in_set<PbrSystems::StartupVxgi>()
        )
        .add_systems(
            RenderUpdate,
            chain(
                compute_scene_aabb,
                prepare_vxgi_voxelization,
                prepare_vxgi_generate_mipmap_volume,
                prepare_inject_radiance,
                prepare_vxgi_resources
            ) | in_set<RenderingSystems::PrepareResources>() |
                in_set<PbrSystems::PrepareVxgi>(),
            chain(
                mark_vxgi_voxelization_dirty,
                queue_vxgi_voxelization_pipelines
            ) | in_set<RenderingSystems::Queue>(),
            chain(
                FEI_NAMED_SYSTEM(render_vxgi_voxelization_pass),
                FEI_NAMED_SYSTEM(render_vxgi_inject_radiance_pass),
                FEI_NAMED_SYSTEM(render_vxgi_mipmap_base_pass),
                FEI_NAMED_SYSTEM(render_vxgi_mipmap_volume_pass),
                FEI_NAMED_SYSTEM(render_vxgi_inject_propagation_pass),
                FEI_NAMED_SYSTEM(
                    render_vxgi_mipmap_base_after_propagation_pass
                ),
                FEI_NAMED_SYSTEM(
                    render_vxgi_mipmap_volume_after_propagation_pass
                )
            ) | in_set<RenderingSystems::Prepass>() |
                in_set<PbrSystems::VxgiPass>()
        );
}

} // namespace fei
