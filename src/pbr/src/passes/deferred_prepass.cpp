#include "pbr/graph_resources.hpp"
#include "pbr/mesh_queue.hpp"
#include "pbr/passes/deferred_internal.hpp"
#include "pbr/pipeline_specializer.hpp"

#include <memory>
#include <vector>

namespace fei {

namespace {

OutputDescription deferred_gbuffer_output_description() {
    return OutputDescription {
        .color_attachments =
            {
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba16Float,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba16Float,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba8Unorm,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba8Unorm,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba16Float,
                },
            },
        .depth_stencil_attachment =
            OutputAttachmentDescription {
                .format = PixelFormat::Depth32Float,
            },
        .sample_count = TextureSampleCount::Count1,
    };
}

class DeferredPipelineSpecializer : public PipelineSpecializer {
  public:
    explicit DeferredPipelineSpecializer(
        const PbrMeshShaderDefaults& /*shader_defaults*/
    ) {}

    MaterialShaderType vertex_shader_type() const override {
        return MaterialShaderType::PrepassVertex;
    }

    MaterialShaderType fragment_shader_type() const override {
        return MaterialShaderType::PrepassFragment;
    }

    BitFlags<PbrMeshPipelineKeyFlags> mesh_pipeline_flags() const override {
        return {
            PbrMeshPipelineKeyFlags::DepthPrepass,
            PbrMeshPipelineKeyFlags::DeferredPrepass,
            PbrMeshPipelineKeyFlags::PrepassReadsMaterial,
        };
    }

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh&,
        const PreparedMaterial&
    ) const override {
        desc.output_description = deferred_gbuffer_output_description();
    }
};

struct DeferredPrepassDrawItem {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<const ResourceSet> view_set;
    std::shared_ptr<const ResourceSet> mesh_set;
    std::shared_ptr<const ResourceSet> material_set;
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    uint32 index_count {};
    uint32 vertex_count {};
};

struct DeferredPrepassPassData {
    DeferredGBufferGraphHandles gbuffer;
    std::vector<DeferredPrepassDrawItem> draw_items;
};

TextureDescription
make_render_texture_desc(uint32 width, uint32 height, PixelFormat format) {
    return TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = format,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    };
}

void draw_deferred_prepass_item(
    CommandBuffer& command_buffer,
    const DeferredPrepassDrawItem& item
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

} // namespace

void queue_deferred_prepass_meshes(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    Query<Entity, const MeshViewResourceSet>::Filter<With<Camera3d>>
        query_cameras,
    ResRW<DeferredPrepassPhase> phase,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<ViewVisibleEntities> visible_entities,
    ResRO<PbrMeshShaderDefaults> shader_defaults,
    ResRW<PipelineCache>
) {
    phase->clear();

    auto [camera_entity, mesh_view_resource_set] = query_cameras.first();
    auto visible_meshes =
        visible_entities->get(ViewId::from_source(camera_entity));
    if (!visible_meshes) {
        return;
    }

    queue_mesh_draw_items(
        query_meshes,
        *phase,
        mesh_view_resource_set.resource_set,
        *gpu_meshes,
        *materials,
        *mesh_uniforms,
        *mesh_material_pipelines,
        DeferredPipelineSpecializer {*shader_defaults},
        [visible_meshes](
            Entity entity,
            const Mesh3d&,
            const MeshMaterial3d<StandardMaterial>&,
            const Transform3d&
        ) {
            return visible_meshes->contains(entity);
        }
    );
}

void build_deferred_prepass(
    ResRW<RenderGraph> render_graph,
    ResRO<DeferredPrepassPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<Window> window,
    ResRO<PipelineCache> pipeline_cache
) {
    auto& blackboard_gbuffer =
        render_graph->blackboard().emplace<DeferredGBufferGraphHandles>();

    std::vector<DeferredPrepassDrawItem> draw_items;
    draw_items.reserve(phase->items.size());
    for (const auto& item : phase->items) {
        draw_items.push_back(
            DeferredPrepassDrawItem {
                .pipeline = pipeline_cache->get_render_pipeline(item.pipeline),
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

    render_graph->add_pass<DeferredPrepassPassData>(
        "deferred_prepass",
        [&](RenderGraphBuilder& builder, DeferredPrepassPassData& data) {
            data.draw_items = std::move(draw_items);
            auto& gbuffer = data.gbuffer;

            gbuffer.position_ao = builder.create_texture(
                "g_position_ao",
                make_render_texture_desc(
                    window->width,
                    window->height,
                    PixelFormat::Rgba16Float
                )
            );
            gbuffer.normal_roughness = builder.create_texture(
                "g_normal_roughness",
                make_render_texture_desc(
                    window->width,
                    window->height,
                    PixelFormat::Rgba16Float
                )
            );
            gbuffer.albedo_metallic = builder.create_texture(
                "g_albedo_metallic",
                make_render_texture_desc(
                    window->width,
                    window->height,
                    PixelFormat::Rgba8Unorm
                )
            );
            gbuffer.specular = builder.create_texture(
                "g_specular",
                make_render_texture_desc(
                    window->width,
                    window->height,
                    PixelFormat::Rgba8Unorm
                )
            );
            gbuffer.emissive_depth = builder.create_texture(
                "g_emissive_depth",
                make_render_texture_desc(
                    window->width,
                    window->height,
                    PixelFormat::Rgba16Float
                )
            );
            gbuffer.depth =
                builder.import_texture("camera_depth", target->depth_texture);

            gbuffer.position_ao = builder.write_texture(
                gbuffer.position_ao,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.normal_roughness = builder.write_texture(
                gbuffer.normal_roughness,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.albedo_metallic = builder.write_texture(
                gbuffer.albedo_metallic,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.specular = builder.write_texture(
                gbuffer.specular,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.emissive_depth = builder.write_texture(
                gbuffer.emissive_depth,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.depth = builder.write_texture(
                gbuffer.depth,
                RenderGraphAccess::DepthStencilWrite
            );

            blackboard_gbuffer = gbuffer;
        },
        [](RenderGraphContext& context, const DeferredPrepassPassData& data) {
            const auto& gbuffer = data.gbuffer;
            auto& command_buffer = context.command_buffer();
            command_buffer.begin_render_pass(
                RenderPassDescription {
                    .color_attachments =
                        {
                            {
                                .texture = context.texture(gbuffer.position_ao),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                            },
                            {
                                .texture =
                                    context.texture(gbuffer.normal_roughness),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                            {
                                .texture =
                                    context.texture(gbuffer.albedo_metallic),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                            {
                                .texture = context.texture(gbuffer.specular),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                            {
                                .texture =
                                    context.texture(gbuffer.emissive_depth),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                        },
                    .depth_stencil_attachment =
                        RenderPassDepthStencilAttachment {
                            .texture = context.texture(gbuffer.depth),
                            .depth_load_op = LoadOp::Clear,
                            .stencil_load_op = LoadOp::Clear,
                            .clear_depth = 1.0f,
                            .clear_stencil = 0,
                        },
                }
            );
            auto color = context.texture(gbuffer.position_ao);
            command_buffer.set_viewport(0, 0, color->width(), color->height());
            for (const auto& item : data.draw_items) {
                draw_deferred_prepass_item(command_buffer, item);
            }
            command_buffer.end_render_pass();
        }
    );
}

} // namespace fei
