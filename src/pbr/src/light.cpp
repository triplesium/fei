#include "pbr/light.hpp"

#include "base/hash.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "pbr/graph_resources.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace fei {

namespace {

struct alignas(16) BlurUniform {
    alignas(16) Vector2 direction;
    int type = 1;
};

struct ShadowMapDrawItem {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<const ResourceSet> view_set;
    std::shared_ptr<const ResourceSet> mesh_set;
    std::shared_ptr<const ResourceSet> material_set;
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    uint32 index_count {};
    uint32 vertex_count {};
};

struct ShadowMapRenderPassData {
    std::vector<ShadowMapDrawItem> draw_items;
    RgTextureHandle shadow;
    RgTextureHandle depth;
};

struct ShadowBlurPassData {
    RgResourceSetHandle resource_set;
    RgTextureHandle target;
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<const Buffer> quad_vertex_buffer;
    std::shared_ptr<const Buffer> quad_index_buffer;
    uint32 quad_index_count {};
    uint32 quad_vertex_count {};
    uint32 output_width {};
    uint32 output_height {};
    bool horizontal {true};
    bool generate_mipmaps {false};
};

TextureDescription shadow_depth_texture_desc(const Texture& shadow_texture) {
    return TextureDescription {
        .width = shadow_texture.width(),
        .height = shadow_texture.height(),
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Depth32Float,
        .texture_usage = TextureUsage::DepthStencil,
        .texture_type = TextureType::Texture2D,
    };
}

TextureDescription shadow_blur_texture_desc(const Texture& shadow_texture) {
    return TextureDescription {
        .width = shadow_texture.width(),
        .height = shadow_texture.height(),
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = shadow_texture.format(),
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    };
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

OutputDescription shadow_map_output_description() {
    return OutputDescription {
        .color_attachments =
            {
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba32Float,
                },
            },
        .depth_stencil_attachment =
            OutputAttachmentDescription {
                .format = PixelFormat::Depth32Float,
            },
        .sample_count = TextureSampleCount::Count1,
    };
}

void draw_shadow_blur_quad(
    CommandBuffer& command_buffer,
    const ShadowBlurPassData& data
) {
    command_buffer.set_vertex_buffer(data.quad_vertex_buffer);
    if (data.quad_index_buffer) {
        command_buffer.set_index_buffer(
            data.quad_index_buffer,
            IndexFormat::Uint32
        );
        command_buffer.draw_indexed(data.quad_index_count);
    } else {
        command_buffer.draw(0, data.quad_vertex_count);
    }
}

void draw_shadow_map_item(
    CommandBuffer& command_buffer,
    const ShadowMapDrawItem& item
) {
    if (!item.pipeline) {
        return;
    }

    command_buffer.set_render_pipeline(item.pipeline);
    command_buffer.set_resource_set(0, item.view_set);
    command_buffer.set_resource_set(1, item.mesh_set);
    command_buffer.set_resource_set(2, item.material_set);
    command_buffer.set_vertex_buffer(item.vertex_buffer);

    if (item.index_buffer) {
        command_buffer.set_index_buffer(item.index_buffer, IndexFormat::Uint32);
        command_buffer.draw_indexed(item.index_count);
    } else {
        command_buffer.draw(0, item.vertex_count);
    }
}

void execute_shadow_blur_pass(
    RenderGraphContext& context,
    const ShadowBlurPassData& data
) {
    auto target = context.texture(data.target);
    auto& command_buffer = context.command_buffer();
    constexpr float blur_scale = 1.0f;
    BlurUniform blur_uniform {
        .direction =
            data.horizontal ?
                Vector2 {
                    1.0f / static_cast<float>(data.output_width) * blur_scale,
                    0.0f
                } :
                Vector2 {
                    0.0f,
                    1.0f / static_cast<float>(data.output_height) * blur_scale
                },
        .type = 2,
    };
    command_buffer
        .update_buffer(data.uniform_buffer, &blur_uniform, sizeof(BlurUniform));
    command_buffer.begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                RenderPassColorAttachment {
                    .texture = target,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                },
            },
        }
    );
    command_buffer.set_viewport(0, 0, data.output_width, data.output_height);

    command_buffer.set_render_pipeline(data.pipeline);
    command_buffer.set_resource_set(0, context.resource_set(data.resource_set));
    draw_shadow_blur_quad(command_buffer, data);
    command_buffer.end_render_pass();

    if (data.generate_mipmaps) {
        command_buffer.generate_mipmaps(target);
    }
}

} // namespace

ShadowMapPipelineSpecializer::ShadowMapPipelineSpecializer(
    std::vector<std::shared_ptr<const ShaderModule>> shader_modules
) : m_shader_modules(std::move(shader_modules)) {
    for (const auto& shader_module : m_shader_modules) {
        hash_combine(m_cache_key, shader_module.get());
    }
}

std::size_t ShadowMapPipelineSpecializer::cache_key() const {
    return m_cache_key;
}

void ShadowMapPipelineSpecializer::specialize(
    RenderPipelineDescription& desc,
    const GpuMesh&,
    const PreparedMaterial&
) const {
    desc.shader_program.shaders = m_shader_modules;
    remove_vertex_input_attribute(desc, Mesh::ATTRIBUTE_NORMAL.id);
    remove_vertex_input_attribute(desc, Mesh::ATTRIBUTE_TANGENT.id);
    desc.depth_stencil_state = DepthStencilStateDescription::DepthOnlyLessEqual;
    desc.rasterizer_state =
        RasterizerStateDescription {.cull_mode = CullMode::Back};
    desc.blend_state = BlendStateDescription::SingleDisabled;
    desc.output_description = shadow_map_output_description();
}

void init_light_view_uniform_buffer(
    Query<Entity, const DirectionalLight, const Transform3d>::Filter<
        Without<ViewUniformBuffer>> query_light,
    ResRO<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, light, transform] : query_light) {
        auto buffer = device->create_buffer(
            BufferDescription {
                .size = sizeof(ViewUniform),
                .usages = BufferUsages::Uniform,
            }
        );
        commands.entity(entity).add(ViewUniformBuffer {.buffer = buffer});
    }
}

void prepare_light_view_uniform_buffer(
    Query<Entity, const DirectionalLight, const Transform3d, ViewUniformBuffer>
        query_light,
    ResRO<GraphicsDevice> device
) {
    for (auto [entity, light, transform, view_uniform_buffer] : query_light) {
        auto view = look_at(
            transform.position,
            transform.position + transform.forward(),
            Vector3 {0.0f, 1.0f, 0.0f}
        );
        const float proj_size = light.projection_size;
        auto proj = orthographic(
            -proj_size,
            proj_size,
            proj_size,
            -proj_size,
            0.1f,
            2 * proj_size
        );
        auto logical_clip_from_world = proj * view;
        auto clip_space_transform = device->clip_space_transform();
        auto uniform = ViewUniform {
            .clip_from_world = clip_space_transform * logical_clip_from_world,
            .view_from_world = view,
            .clip_from_view = clip_space_transform * proj,
            .world_position = transform.position,
        };
        view_uniform_buffer.uniform = uniform;
        view_uniform_buffer.view = RenderView {
            .kind = RenderViewKind::DirectionalShadow,
            .id = ViewId::from_source(entity),
            .clip_from_world = logical_clip_from_world,
            .view_from_world = uniform.view_from_world,
            .clip_from_view = proj,
            .world_position = uniform.world_position,
            .frustum = extract_frustum(logical_clip_from_world),
        };
        device->update_buffer(
            view_uniform_buffer.buffer,
            0,
            &view_uniform_buffer.uniform,
            sizeof(ViewUniform)
        );
    }
}

RgTextureHandle
first_shadow_map_graph_handle(RenderGraphBlackboard& blackboard) {
    if (!blackboard.contains<ShadowMapGraphHandles>()) {
        return {};
    }
    return blackboard.get<ShadowMapGraphHandles>().first();
}

std::vector<RenderGraphResourceBinding> lighting_resource_bindings(
    const LightingResources& lighting,
    RgTextureHandle shadow_map,
    std::shared_ptr<Texture> fallback_shadow_map
) {
    std::vector<RenderGraphResourceBinding> bindings {
        lighting.uniform_buffer,
    };
    if (shadow_map.is_valid()) {
        bindings.emplace_back(shadow_map);
    } else {
        bindings.emplace_back(std::move(fallback_shadow_map));
    }
    bindings.emplace_back(lighting.shadow_map_sampler);
    return bindings;
}

void setup_lighting(ResRO<GraphicsDevice> device, Commands commands) {
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment, ShaderStages::Compute},
            {
                uniform_buffer("Lighting"),
                texture_read_only("shadow_map"),
                sampler("shadow_map_sampler"),
            }
        )
    );
    auto uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(LightingUniform),
            .usages = BufferUsages::Uniform,
        }
    );

    commands.add_resource(
        LightingResources {
            .uniform_buffer = uniform_buffer,
            .resource_layout = resource_layout,
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

void prepare_lighting(
    Query<
        const DirectionalLight,
        const Transform3d,
        const ViewUniformBuffer,
        const ShadowMap> query_directional_lights,
    Query<const PointLight, const Transform3d> query_point_lights,
    ResRW<LightingResources> lighting,
    ResRO<GraphicsDevice> device
) {
    LightingUniform uniform {};

    std::size_t dir_light_count = 0;
    for (const auto& [light, transform, view_uniform_buffer, shadow_map] :
         query_directional_lights) {
        (void)shadow_map;
        if (dir_light_count >= uniform.directional_lights.size()) {
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
            view_uniform_buffer.view.clip_from_world;
        ++dir_light_count;
    }
    uniform.num_directional_lights = static_cast<uint32>(dir_light_count);

    std::size_t point_light_count = 0;
    for (const auto& [light, transform] : query_point_lights) {
        if (point_light_count >= uniform.point_lights.size()) {
            break;
        }
        auto& point_light = uniform.point_lights[point_light_count];
        point_light.diffuse = light.color.to_vector3() * light.intensity;
        point_light.specular = point_light.diffuse;
        point_light.position = transform.position;
        point_light.shadowing_method = 2;
        ++point_light_count;
    }
    uniform.num_point_lights = static_cast<uint32>(point_light_count);

    device->update_buffer(
        lighting->uniform_buffer,
        0,
        &uniform,
        sizeof(LightingUniform)
    );
}

void setup_shadow_mapping(
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    ResRO<Assets<Mesh>> mesh_assets,
    ResRO<FullscreenQuad> fs_quad,
    Commands commands
) {
    std::vector<std::shared_ptr<const ShaderModule>> shadow_shader_modules {
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/shadow.slang"),
            ShaderStages::Vertex,
            {}
        ),
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/shadow.slang"),
            ShaderStages::Fragment,
            {}
        ),
    };

    commands.add_resource(
        ShadowMappingResources {
            .pipeline_specializer =
                ShadowMapPipelineSpecializer {shadow_shader_modules},
            .temp_depth_texture = device->create_texture(
                TextureDescription {
                    .width = 1024,
                    .height = 1024,
                    .depth = 1,
                    .mip_level = 1,
                    .layer = 1,
                    .texture_format = PixelFormat::Depth32Float,
                    .texture_usage =
                        {TextureUsage::RenderTarget, TextureUsage::Sampled},
                    .texture_type = TextureType::Texture2D,
                }
            )
        }
    );

    std::vector<std::shared_ptr<const ShaderModule>> blur_shader_modules {
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/quad.slang"),
            ShaderStages::Vertex,
            {}
        ),
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/blur.slang"),
            ShaderStages::Fragment,
            {}
        ),
    };

    auto blur_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                uniform_buffer("Blur"),
                texture_read_only("source"),
                sampler("sampler"),
            }
        )
    );

    auto blur_sampler = device->create_sampler(SamplerDescription::Linear);

    auto blur_uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(BlurUniform),
            .usages = BufferUsages::Uniform,
        }
    );

    auto& quad_mesh = mesh_assets->get(fs_quad->fullscreen_quad_mesh).value();
    auto blur_vertex_layout =
        quad_mesh.vertex_buffer_layout().to_vertex_layout_description();
    remove_vertex_input_attribute(
        blur_vertex_layout,
        Mesh::ATTRIBUTE_NORMAL.id
    );
    remove_vertex_input_attribute(
        blur_vertex_layout,
        Mesh::ATTRIBUTE_TANGENT.id
    );

    auto blur_pipeline = device->create_render_pipeline(
        RenderPipelineDescription {
            .depth_stencil_state = DepthStencilStateDescription::Disabled,
            .rasterizer_state =
                RasterizerStateDescription {.cull_mode = CullMode::None},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                {
                    .vertex_layouts = {blur_vertex_layout},
                    .shaders = blur_shader_modules,
                },
            .resource_layouts = {blur_resource_layout},
            .output_description =
                single_color_output_description(PixelFormat::Rgba32Float),
        }
    );

    commands.add_resource(
        BlurResources {
            .pipeline = blur_pipeline,
            .resource_layout = blur_resource_layout,
            .uniform_buffer = blur_uniform_buffer,
            .sampler = blur_sampler,
        }
    );
}

void setup_shadow_map(
    Query<Entity, const DirectionalLight, const Transform3d>::Filter<
        Without<ShadowMap>> query_light,
    ResRO<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, light, transform] : query_light) {
        if (!light.shadow_map_enabled) {
            continue;
        }
        auto shadow_map_texture = device->create_texture(
            TextureDescription {
                // TODO: make shadow map resolution configurable
                .width = 1024,
                .height = 1024,
                .depth = 1,
                .mip_level = static_cast<uint32>(std::log2(1024) + 1),
                .layer = 1,
                .texture_format = PixelFormat::Rgba32Float,
                .texture_usage =
                    {TextureUsage::RenderTarget,
                     TextureUsage::Sampled,
                     TextureUsage::GenerateMipmaps},
                .texture_type = TextureType::Texture2D,
            }
        );
        commands.entity(entity).add(
            ShadowMap {
                .texture = shadow_map_texture,
            }
        );
    }
}

void queue_shadow_map_meshes(
    Query<
        Entity,
        const DirectionalLight,
        const Transform3d,
        const MeshViewResourceSet,
        const ShadowMap> query_light,
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<ShadowMappingResources> shadow_mapping_resources,
    ResRO<ViewVisibleEntities> visible_entities,
    ResRW<ShadowMapPhase> phase
) {
    phase->clear();

    for (auto [light_entity, light, transform, view_resource_set, shadow_map] :
         query_light) {
        if (!light.shadow_map_enabled) {
            continue;
        }
        auto light_view_id = ViewId::from_source(light_entity);
        auto visible_meshes = visible_entities->get(light_view_id);
        if (!visible_meshes) {
            continue;
        }

        auto& pass = phase->passes.emplace_back();
        pass.view = light_view_id;
        pass.texture = shadow_map.texture;

        for (auto [entity, mesh, mesh_material, mesh_transform] :
             query_meshes) {
            if (!mesh.cast_shadow || !visible_meshes->contains(entity)) {
                continue;
            }
            auto gpu_mesh_opt = gpu_meshes->get(mesh.mesh);
            auto material_opt = materials->get(mesh_material.material);
            auto mesh_uniform_it = mesh_uniforms->entries.find(entity);
            if (!gpu_mesh_opt || !material_opt ||
                mesh_uniform_it == mesh_uniforms->entries.end()) {
                continue;
            }

            auto& gpu_mesh = *gpu_mesh_opt;
            auto& material = *material_opt;
            auto pipeline_id = mesh_material_pipelines->request(
                entity,
                material,
                gpu_mesh,
                shadow_mapping_resources->pipeline_specializer
            );
            pass.items.push_back(make_mesh_draw_item(
                entity,
                pipeline_id,
                view_resource_set.resource_set,
                mesh_uniform_it->second.resource_set,
                material.resource_set(),
                gpu_mesh
            ));
        }

        sort_by_pipeline(pass);
    }
}

void build_shadow_map_passes(
    ResRW<RenderGraph> render_graph,
    ResRO<ShadowMapPhase> phase,
    ResRO<PipelineCache> pipeline_cache
) {
    if (phase->passes.empty()) {
        return;
    }

    auto& handles = render_graph->blackboard().emplace<ShadowMapGraphHandles>();

    for (const auto& pass : phase->passes) {
        std::vector<ShadowMapDrawItem> draw_items;
        draw_items.reserve(pass.items.size());
        for (const auto& item : pass.items) {
            draw_items.push_back(
                ShadowMapDrawItem {
                    .pipeline =
                        pipeline_cache->get_render_pipeline(item.pipeline),
                    .view_set = item.view_set,
                    .mesh_set = item.mesh_set,
                    .material_set = item.material_set,
                    .vertex_buffer = item.vertex_buffer,
                    .index_buffer = item.index_buffer,
                    .index_count = item.index_count,
                    .vertex_count = item.vertex_count,
                }
            );
        }
        auto shadow_texture = pass.texture;
        render_graph->add_pass<ShadowMapRenderPassData>(
            "shadow_map",
            [&](RenderGraphBuilder& builder, ShadowMapRenderPassData& data) {
                data.draw_items = std::move(draw_items);
                auto shadow =
                    builder.import_texture("shadow_map", shadow_texture);
                data.shadow = builder.write_texture(
                    shadow,
                    RenderGraphAccess::ColorAttachmentWrite
                );
                auto depth = builder.create_texture(
                    "shadow_depth",
                    shadow_depth_texture_desc(*shadow_texture)
                );
                data.depth = builder.write_texture(
                    depth,
                    RenderGraphAccess::DepthStencilWrite
                );
                handles.entries.push_back(
                    ShadowMapGraphEntry {
                        .texture = shadow_texture,
                        .handle = data.shadow,
                    }
                );
            },
            [](RenderGraphContext& context,
               const ShadowMapRenderPassData& data) {
                auto shadow = context.texture(data.shadow);
                auto depth = context.texture(data.depth);
                auto& command_buffer = context.command_buffer();

                command_buffer.begin_render_pass(
                    RenderPassDescription {
                        .color_attachments =
                            {
                                RenderPassColorAttachment {
                                    .texture = shadow,
                                    .load_op = LoadOp::Clear,
                                    .clear_color =
                                        Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                                },
                            },
                        .depth_stencil_attachment =
                            RenderPassDepthStencilAttachment {
                                .texture = depth,
                                .depth_load_op = LoadOp::Clear,
                                .stencil_load_op = LoadOp::Clear,
                                .clear_depth = 1.0f,
                                .clear_stencil = 0,
                            },
                    }
                );
                command_buffer
                    .set_viewport(0, 0, shadow->width(), shadow->height());

                for (const auto& item : data.draw_items) {
                    draw_shadow_map_item(command_buffer, item);
                }
                command_buffer.end_render_pass();
            }
        );
    }
}

void build_shadow_blur_passes(
    ResRW<RenderGraph> render_graph,
    ResRO<BlurResources> blur_resources,
    ResRO<FullscreenQuad> fullscreen_quad_resource,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes
) {
    if (!render_graph->blackboard().contains<ShadowMapGraphHandles>()) {
        return;
    }

    auto quad_mesh_opt =
        gpu_meshes->get(fullscreen_quad_resource->fullscreen_quad_mesh);
    if (!quad_mesh_opt) {
        return;
    }
    auto& quad_mesh = *quad_mesh_opt;
    auto quad_index_buffer = quad_mesh.index_buffer();
    auto blur_pipeline = blur_resources->pipeline;
    auto blur_resource_layout = blur_resources->resource_layout;
    auto blur_uniform_buffer = blur_resources->uniform_buffer;
    auto blur_sampler = blur_resources->sampler;
    auto quad_vertex_buffer = quad_mesh.vertex_buffer();
    auto quad_index_count = static_cast<uint32>(
        quad_mesh.index_buffer_size() / sizeof(std::uint32_t)
    );
    auto quad_vertex_count = static_cast<uint32>(quad_mesh.vertex_count());
    auto& handles = render_graph->blackboard().get<ShadowMapGraphHandles>();

    for (auto& entry : handles.entries) {
        auto& horizontal_data = render_graph->add_pass<ShadowBlurPassData>(
            "shadow_blur_horizontal",
            [&](RenderGraphBuilder& builder, ShadowBlurPassData& data) {
                data.pipeline = blur_pipeline;
                data.uniform_buffer = blur_uniform_buffer;
                data.quad_vertex_buffer = quad_vertex_buffer;
                data.quad_index_buffer =
                    quad_index_buffer ? *quad_index_buffer : nullptr;
                data.quad_index_count = quad_index_count;
                data.quad_vertex_count = quad_vertex_count;
                data.output_width = entry.texture->width();
                data.output_height = entry.texture->height();
                data.horizontal = true;
                data.generate_mipmaps = false;
                data.target = builder.create_texture(
                    "shadow_blur_intermediate",
                    shadow_blur_texture_desc(*entry.texture)
                );
                data.target = builder.write_texture(
                    data.target,
                    RenderGraphAccess::ColorAttachmentWrite
                );
                data.resource_set = builder.create_resource_set(
                    "shadow.blur.horizontal",
                    blur_resource_layout,
                    {blur_uniform_buffer, entry.handle, blur_sampler}
                );
            },
            [](RenderGraphContext& context, const ShadowBlurPassData& data) {
                execute_shadow_blur_pass(context, data);
            }
        );

        auto horizontal_target = horizontal_data.target;
        render_graph->add_pass<ShadowBlurPassData>(
            "shadow_blur_vertical",
            [&](RenderGraphBuilder& builder, ShadowBlurPassData& data) {
                data.pipeline = blur_pipeline;
                data.uniform_buffer = blur_uniform_buffer;
                data.quad_vertex_buffer = quad_vertex_buffer;
                data.quad_index_buffer =
                    quad_index_buffer ? *quad_index_buffer : nullptr;
                data.quad_index_count = quad_index_count;
                data.quad_vertex_count = quad_vertex_count;
                data.output_width = entry.texture->width();
                data.output_height = entry.texture->height();
                data.horizontal = false;
                data.generate_mipmaps = true;
                data.target = builder.write_texture(
                    entry.handle,
                    RenderGraphAccess::ColorAttachmentWrite
                );
                data.resource_set = builder.create_resource_set(
                    "shadow.blur.vertical",
                    blur_resource_layout,
                    {blur_uniform_buffer, horizontal_target, blur_sampler}
                );
                builder.side_effect();
                entry.handle = data.target;
            },
            [](RenderGraphContext& context, const ShadowBlurPassData& data) {
                execute_shadow_blur_pass(context, data);
            }
        );
    }
}

} // namespace fei
