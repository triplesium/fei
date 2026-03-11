#include "pbr/vxgi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fei {

VxgiVoxelizationSpecializer::VxgiVoxelizationSpecializer(
    std::vector<std::shared_ptr<ShaderModule>> shader_modules,
    std::shared_ptr<ResourceLayout> volumes_layout,
    std::shared_ptr<ResourceLayout> voxelization_layout
) :
    m_shader_modules(std::move(shader_modules)),
    m_volumes_layout(std::move(volumes_layout)),
    m_voxelization_layout(std::move(voxelization_layout)) {}

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
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
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
    for (int i = 0; i < 6; ++i) {
        volumes->mipmap[i] = device->create_texture(TextureDescription {
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
        });
        command_buffer->generate_mipmaps(volumes->mipmap[i]);
    }
    device->submit_commands(command_buffer);

    volumes->resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
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
        ));

    volumes->resource_set = device->create_resource_set(ResourceSetDescription {
        .layout = volumes->resource_layout,
        .resources =
            {
                volumes->albedo,
                volumes->normal,
                volumes->emissive,
                volumes->radiance,
                volumes->static_flag,
            },
    });

    std::vector<Handle<Shader>> shader_handles {
        asset_server->load<Shader>("embeded://voxelization.vert"),
        asset_server->load<Shader>("embeded://voxelization.geom"),
        asset_server->load<Shader>("embeded://voxelization.frag"),
    };
    std::vector<std::shared_ptr<ShaderModule>> shader_modules;
    for (const auto& handle : shader_handles) {
        auto shader = shader_assets->get(handle).value();
        shader_modules.push_back(
            device->create_shader_module(shader.description())
        );
    }

    auto voxelization_resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex,
             ShaderStages::Geometry,
             ShaderStages::Fragment,
             ShaderStages::Compute},
            {uniform_buffer("VxgiVoxelization")}
        ));

    auto voxelization_uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(VxgiVoxelizationUniform),
        .usages = BufferUsages::Uniform,
    });

    commands.add_resource(VxgiVoxelization {
        .voxelization_uniform_buffer = voxelization_uniform_buffer,
        .temp_texture = device->create_texture(TextureDescription {
            .width = 1920,
            .height = 1080,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage = TextureUsage::RenderTarget,
            .texture_type = TextureType::Texture2D,
        }),
        .resource_layout = voxelization_resource_layout,
        .resource_set = device->create_resource_set(ResourceSetDescription {
            .layout = voxelization_resource_layout,
            .resources = {voxelization_uniform_buffer},
        }),
        .pipeline_specializer = VxgiVoxelizationSpecializer(
            shader_modules,
            volumes->resource_layout,
            voxelization_resource_layout
        ),
    });
}

void compute_scene_aabb(
    Res<VxgiVoxelization> voxelization,
    Query<Mesh3d, Transform3d, Aabb> query
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
    Res<VxgiVoxelization> voxelization,
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device
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

void voxelize_scene(
    Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d>
        query_meshes,
    Res<VxgiVoxelization> voxelization,
    Res<VxgiVolumes> volumes,
    Res<MeshMaterialPipelines> pipelines,
    Res<PipelineCache> pipeline_cache,
    Res<GraphicsDevice> device,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<RenderAssets<PreparedMaterial>> materials,
    EventReader<SceneSpawnEvent> spawn_events,
    Res<MeshUniforms> mesh_uniforms
) {
    if (!spawn_events.next()) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
        .color_attachments =
            {
                RenderPassColorAttachment {
                    .texture = voxelization->temp_texture,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
    });
    command_buffer->set_viewport(
        0,
        0,
        volumes->config.voxel_resolution,
        volumes->config.voxel_resolution
    );
    for (const auto& [entity, mesh3d, mesh_material3d, transform3d] :
         query_meshes) {
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh);
        auto material_opt = materials->get(mesh_material3d.material);
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        auto pipeline_id = pipelines->get(
            entity,
            material,
            gpu_mesh,
            voxelization->pipeline_specializer
        );
        auto pipeline = pipeline_cache->get_pipeline(pipeline_id);
        command_buffer->set_render_pipeline(pipeline);
        command_buffer->set_resource_set(
            1,
            mesh_uniforms->entries.at(entity).resource_set
        );
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
    device->submit_commands(command_buffer);
}

void setup_vxgi_generate_mipmap_base(
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("embeded://aniso_mipmapbase.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
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
        ));
    auto pipeline = device->create_compute_pipeline(ComputePipelineDescription {
        .shader = shader_module,
        .resource_layouts = {resource_layout},
    });
    auto uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(VxgiGenerateMipmapBase::Uniform),
        .usages = BufferUsages::Uniform,
    });
    VxgiGenerateMipmapBase::Uniform uniform {
        .mip_dimension = static_cast<int>(volumes->config.voxel_resolution / 2),
    };
    device->update_buffer(
        uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiGenerateMipmapBase::Uniform)
    );
    auto resource_set = device->create_resource_set(ResourceSetDescription {
        .layout = resource_layout,
        .resources =
            {
                uniform_buffer,
                volumes->radiance,
                volumes->mipmap[0],
                volumes->mipmap[1],
                volumes->mipmap[2],
                volumes->mipmap[3],
                volumes->mipmap[4],
                volumes->mipmap[5],
            },
    });

    commands.add_resource(VxgiGenerateMipmapBase {
        .pipeline = pipeline,
        .resource_layout = resource_layout,
        .resource_set = resource_set,
    });
}

void generate_mipmap_base(
    Res<VxgiVolumes> volumes,
    Res<VxgiGenerateMipmapBase> generate_mipmap_base,
    Res<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->set_compute_pipeline(generate_mipmap_base->pipeline);
    command_buffer->set_resource_set(0, generate_mipmap_base->resource_set);
    auto work_groups = (volumes->config.voxel_resolution / 2) / 8;
    command_buffer->dispatch(work_groups, work_groups, work_groups);
    device->submit_commands(command_buffer);
}

void setup_vxgi_generate_mipmap_volume(
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("embeded://aniso_mipmapvolume.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
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
        ));
    auto pipeline = device->create_compute_pipeline(ComputePipelineDescription {
        .shader = shader_module,
        .resource_layouts = {resource_layout},
    });
    auto uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(VxgiGenerateMipmapVolume::Uniform),
        .usages = BufferUsages::Uniform,
    });
    commands.add_resource(VxgiGenerateMipmapVolume {
        .pipeline = pipeline,
        .resource_layout = resource_layout,
        .uniform_buffer = uniform_buffer,
    });
}

void generate_mipmap_volume(
    Res<VxgiVolumes> volumes,
    Res<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    Res<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->set_compute_pipeline(generate_mipmap_volume->pipeline);
    uint32 mip_dimension = volumes->config.voxel_resolution / 4;
    uint32 mip_level = 0;

    while (mip_dimension > 0) {
        VxgiGenerateMipmapVolume::Uniform uniform {
            .mip_dimension = Vector3 {static_cast<float>(mip_dimension)},
            .mip_level = static_cast<int>(mip_level),
        };
        device->update_buffer(
            generate_mipmap_volume->uniform_buffer,
            0,
            &uniform,
            sizeof(VxgiGenerateMipmapVolume::Uniform)
        );
        std::array<std::shared_ptr<TextureView>, 6> dst_views;
        for (int i = 0; i < 6; ++i) {
            dst_views[i] = device->create_texture_view(TextureViewDescription {
                .target = volumes->mipmap[i],
                .base_mip_level = mip_level + 1,
            });
        }
        auto resource_set = device->create_resource_set(ResourceSetDescription {
            .layout = generate_mipmap_volume->resource_layout,
            .resources =
                {
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
        });
        command_buffer->set_resource_set(0, resource_set);
        auto work_groups = (mip_dimension + 7) / 8;
        command_buffer->dispatch(work_groups, work_groups, work_groups);
        mip_dimension /= 2;
        ++mip_level;
    }
    device->submit_commands(command_buffer);
}

void setup_inject_radiance(
    Res<VxgiVolumes> volumes,
    Res<VxgiVoxelization> voxelization,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("embeded://inject_radiance.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
            {ShaderStages::Compute},
            {
                uniform_buffer("VxgiInjectRadiance"),
                texture_read_only("shadow_map"),
                sampler("shadow_map_sampler"),
            }
        ));
    auto pipeline = device->create_compute_pipeline(ComputePipelineDescription {
        .shader = shader_module,
        .resource_layouts =
            {
                volumes->resource_layout,
                voxelization->resource_layout,
                resource_layout,
            },
    });
    auto uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(VxgiInjectRadianceUniform),
        .usages = BufferUsages::Uniform,
    });

    commands.add_resource(VxgiInjectRadiance {
        .pipeline = pipeline,
        .uniform_buffer = uniform_buffer,
        .resource_layout = resource_layout,
        .resource_set = nullptr,
    });
}

void prepare_inject_radiance(
    Query<DirectionalLight, Transform3d, ViewUniformBuffer, ShadowMap>
        query_directional_lights,
    Query<PointLight, Transform3d> query_point_lights,
    Res<VxgiInjectRadiance> inject_radiance,
    Res<GraphicsDevice> device,
    Res<RenderingDefaults> rendering_defaults
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

    inject_radiance->resource_set =
        device->create_resource_set(ResourceSetDescription {
            .layout = inject_radiance->resource_layout,
            .resources =
                {inject_radiance->uniform_buffer,
                 shadow_map_texture ? shadow_map_texture :
                                      rendering_defaults->default_texture,
                 device->create_sampler(SamplerDescription {
                     .address_mode_u = SamplerAddressMode::ClampToEdge,
                     .address_mode_v = SamplerAddressMode::ClampToEdge,
                     .address_mode_w = SamplerAddressMode::ClampToEdge,
                 })},
        });
}

void inject_radiance(
    Res<VxgiVolumes> volumes,
    Res<VxgiVoxelization> voxelization,
    Res<VxgiInjectRadiance> inject_radiance,
    Res<VxgiGenerateMipmapBase> mipmap_base,
    Res<VxgiGenerateMipmapVolume> mipmap_volume,
    Res<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->set_compute_pipeline(inject_radiance->pipeline);
    command_buffer->set_resource_set(0, volumes->resource_set);
    command_buffer->set_resource_set(1, voxelization->resource_set);
    command_buffer->set_resource_set(2, inject_radiance->resource_set);
    auto work_groups = (volumes->config.voxel_resolution) / 8;
    command_buffer->dispatch(work_groups, work_groups, work_groups);
    device->submit_commands(command_buffer);

    generate_mipmap_base(volumes, mipmap_base, device);
    generate_mipmap_volume(volumes, mipmap_volume, device);
}

void setup_inject_propagation(
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
) {
    auto shader_handle =
        asset_server->load<Shader>("embeded://inject_propagation.comp");
    auto shader = shader_assets->get(shader_handle).value();
    auto shader_module = device->create_shader_module(shader.description());
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
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
        ));
    auto pipeline = device->create_compute_pipeline(ComputePipelineDescription {
        .shader = shader_module,
        .resource_layouts = {resource_layout},
    });
    auto uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(VxgiInjectPropagationUniform),
        .usages = BufferUsages::Uniform,
    });
    VxgiInjectPropagationUniform uniform {
        .volume_dimension = static_cast<int>(volumes->config.voxel_resolution),
    };
    device->update_buffer(
        uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiInjectPropagationUniform)
    );
    auto resource_set = device->create_resource_set(ResourceSetDescription {
        .layout = resource_layout,
        .resources =
            {
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
                device->create_sampler(SamplerDescription {
                    .address_mode_u = SamplerAddressMode::ClampToEdge,
                    .address_mode_v = SamplerAddressMode::ClampToEdge,
                    .address_mode_w = SamplerAddressMode::ClampToEdge,
                }),
            },
    });

    commands.add_resource(VxgiInjectPropagation {
        .pipeline = pipeline,
        .resource_layout = resource_layout,
        .resource_set = resource_set,
        .uniform_buffer = uniform_buffer,
    });
}

void inject_propagation(
    Res<VxgiVolumes> volumes,
    Res<VxgiInjectPropagation> inject_propagation,
    Res<VxgiGenerateMipmapBase> mipmap_base,
    Res<VxgiGenerateMipmapVolume> mipmap_volume,
    Res<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->set_compute_pipeline(inject_propagation->pipeline);
    command_buffer->set_resource_set(0, inject_propagation->resource_set);
    auto work_groups = (volumes->config.voxel_resolution) / 8;
    command_buffer->dispatch(work_groups, work_groups, work_groups);
    device->submit_commands(command_buffer);
    generate_mipmap_base(volumes, mipmap_base, device);
    generate_mipmap_volume(volumes, mipmap_volume, device);
}

void setup_vxgi_lighting(Res<GraphicsDevice> device, Commands commands) {
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
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
        ));
    auto uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(VxgiLightingUniform),
        .usages = BufferUsages::Uniform,
    });

    commands.add_resource(VxgiLighting {
        .uniform_buffer = uniform_buffer,
        .resource_layout = resource_layout,
        .resource_set = nullptr,
    });
}

void prepare_vxgi_lighting(
    Query<DirectionalLight, Transform3d, ViewUniformBuffer, ShadowMap>
        query_directional_lights,
    Query<PointLight, Transform3d> query_point_lights,
    Res<VxgiLighting> vxgi_lighting,
    Res<VxgiVolumes> volumes,
    Res<VxgiVoxelization> voxelization,
    Res<GraphicsDevice> device,
    Res<RenderingDefaults> rendering_defaults
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

    auto uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(VxgiLightingUniform),
        .usages = BufferUsages::Uniform,
    });
    device->update_buffer(
        uniform_buffer,
        0,
        &uniform,
        sizeof(VxgiLightingUniform)
    );
    auto resource_set = device->create_resource_set(ResourceSetDescription {
        .layout = vxgi_lighting->resource_layout,
        .resources =
            {
                uniform_buffer,
                volumes->normal,
                volumes->radiance,
                volumes->mipmap[0],
                volumes->mipmap[1],
                volumes->mipmap[2],
                volumes->mipmap[3],
                volumes->mipmap[4],
                volumes->mipmap[5],
                device->create_sampler(SamplerDescription {
                    .address_mode_u = SamplerAddressMode::ClampToEdge,
                    .address_mode_v = SamplerAddressMode::ClampToEdge,
                    .address_mode_w = SamplerAddressMode::ClampToEdge,
                }),
                shadow_map ? shadow_map : rendering_defaults->default_texture,
                device->create_sampler(SamplerDescription {
                    .address_mode_u = SamplerAddressMode::ClampToEdge,
                    .address_mode_v = SamplerAddressMode::ClampToEdge,
                    .address_mode_w = SamplerAddressMode::ClampToEdge,
                }),
            },
    });

    vxgi_lighting->resource_set = resource_set;
}

void VxgiPlugin::setup(App& app) {
    app.add_resource<VxgiVolumes>()
        .add_systems(
            StartUp,
            setup_vxgi,
            setup_inject_radiance,
            setup_inject_propagation,
            setup_vxgi_generate_mipmap_base,
            setup_vxgi_generate_mipmap_volume,
            setup_vxgi_lighting
        )
        .add_systems(
            RenderUpdate,
            chain(
                compute_scene_aabb,
                prepare_vxgi_voxelization,
                prepare_inject_radiance,
                prepare_vxgi_lighting
            ) | after(setup_shadow_map) |
                in_set<RenderingSystems::PrepareResources>(),
            chain(voxelize_scene, inject_radiance, inject_propagation) |
                in_set<RenderingSystems::Render>()
        );
}

} // namespace fei
