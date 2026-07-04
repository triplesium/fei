#include "pbr/graph_resources.hpp"
#include "pbr/passes/deferred_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

namespace fei {

namespace {

struct FullscreenQuadPassData {
    std::shared_ptr<const ResourceSet> view_set;
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    uint32 index_count {};
    uint32 vertex_count {};
};

struct DeferredLightingPassData {
    RgResourceSetHandle gbuffer_set;
    RgResourceSetHandle lighting_set;
    RgResourceSetHandle vxgi_set;
    RgTextureHandle target;
    FullscreenQuadPassData fullscreen_quad;
    std::shared_ptr<Pipeline> pipeline;
};

struct DeferredCompositePassData {
    RgResourceSetHandle gbuffer_set;
    RgResourceSetHandle composite_set;
    RgTextureHandle target;
    FullscreenQuadPassData fullscreen_quad;
    std::shared_ptr<Pipeline> pipeline;
};

struct BlitCompositePassData {
    RgTextureHandle composite;
    std::shared_ptr<const Framebuffer> target_framebuffer;
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

FullscreenQuadPassData make_fullscreen_quad_pass_data(
    std::shared_ptr<const ResourceSet> view_set,
    const GpuMesh& mesh
) {
    auto index_buffer = mesh.index_buffer();
    return FullscreenQuadPassData {
        .view_set = std::move(view_set),
        .vertex_buffer = mesh.vertex_buffer(),
        .index_buffer = index_buffer ? *index_buffer : nullptr,
        .index_count = static_cast<uint32>(
            mesh.index_buffer_size() / sizeof(std::uint32_t)
        ),
        .vertex_count = static_cast<uint32>(mesh.vertex_count()),
    };
}

void draw_fullscreen_quad(
    CommandBuffer& command_buffer,
    const FullscreenQuadPassData& quad
) {
    command_buffer.set_vertex_buffer(quad.vertex_buffer);
    if (quad.index_buffer) {
        command_buffer.set_index_buffer(quad.index_buffer, IndexFormat::Uint32);
        command_buffer.draw_indexed(quad.index_count);
    } else {
        command_buffer.draw(0, quad.vertex_count);
    }
}

template<typename Draw>
void execute_fullscreen_lighting_pass(
    RenderGraphContext& context,
    const FullscreenQuadPassData& fullscreen_quad,
    std::shared_ptr<Pipeline> pipeline,
    std::shared_ptr<Texture> target,
    Draw&& draw
) {
    auto& command_buffer = context.command_buffer();
    command_buffer.begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                {
                    .texture = target,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            }
        }
    );
    command_buffer.set_viewport(0, 0, target->width(), target->height());
    if (!pipeline) {
        command_buffer.end_render_pass();
        return;
    }

    command_buffer.set_render_pipeline(std::move(pipeline));
    command_buffer.set_resource_set(0, fullscreen_quad.view_set);
    std::forward<Draw>(draw)(command_buffer);
    draw_fullscreen_quad(command_buffer, fullscreen_quad);
    command_buffer.end_render_pass();
}

} // namespace

void build_direct_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<LightingResources> lighting_resources,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Window> window,
    ResRO<RenderingDefaults> rendering_defaults
) {
    if (!render_graph->blackboard().contains<DeferredGBufferGraphHandles>()) {
        return;
    }

    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto fullscreen_quad_data = make_fullscreen_quad_pass_data(
        mesh_view_resource_set.resource_set,
        gpu_mesh.value()
    );
    auto pipeline = pipeline_cache->get_render_pipeline(
        pipelines->direct_lighting_pipeline
    );

    auto& lighting =
        render_graph->blackboard().contains<DeferredLightingGraphHandles>() ?
            render_graph->blackboard().get<DeferredLightingGraphHandles>() :
            render_graph->blackboard().emplace<DeferredLightingGraphHandles>();
    const auto& gbuffer =
        render_graph->blackboard().get<DeferredGBufferGraphHandles>();

    render_graph->add_pass<DeferredLightingPassData>(
        "direct_lighting",
        [&](RenderGraphBuilder& builder, DeferredLightingPassData& data) {
            data.fullscreen_quad = fullscreen_quad_data;
            data.pipeline = pipeline;
            data.gbuffer_set = builder.create_resource_set(
                "deferred.gbuffer",
                pipelines->gbuffer_resource_layout,
                deferred_gbuffer_resource_bindings(
                    gbuffer,
                    pipelines->point_sampler
                )
            );
            data.lighting_set = builder.create_resource_set(
                "lighting",
                lighting_resources->resource_layout,
                lighting_resource_bindings(
                    *lighting_resources,
                    first_shadow_map_graph_handle(render_graph->blackboard()),
                    rendering_defaults->default_texture
                )
            );
            data.target = builder.create_texture(
                "direct_lighting",
                make_render_texture_desc(
                    window->width,
                    window->height,
                    PixelFormat::Rgba16Float
                )
            );
            data.target = builder.write_texture(
                data.target,
                RenderGraphAccess::ColorAttachmentWrite
            );
            lighting.direct = data.target;
        },
        [](RenderGraphContext& context, const DeferredLightingPassData& data) {
            auto gbuffer_set = context.resource_set(data.gbuffer_set);
            auto lighting_set = context.resource_set(data.lighting_set);
            auto target = context.texture(data.target);
            execute_fullscreen_lighting_pass(
                context,
                data.fullscreen_quad,
                data.pipeline,
                target,
                [gbuffer_set, lighting_set](CommandBuffer& command_buffer) {
                    command_buffer.set_resource_set(1, gbuffer_set);
                    command_buffer.set_resource_set(2, lighting_set);
                }
            );
        }
    );
}

void build_indirect_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<VxgiResources> vxgi_resources,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Window> window
) {
    if (!render_graph->blackboard().contains<DeferredGBufferGraphHandles>()) {
        return;
    }

    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto fullscreen_quad_data = make_fullscreen_quad_pass_data(
        mesh_view_resource_set.resource_set,
        gpu_mesh.value()
    );
    auto pipeline = pipeline_cache->get_render_pipeline(
        pipelines->indirect_lighting_pipeline
    );

    auto& lighting =
        render_graph->blackboard().contains<DeferredLightingGraphHandles>() ?
            render_graph->blackboard().get<DeferredLightingGraphHandles>() :
            render_graph->blackboard().emplace<DeferredLightingGraphHandles>();
    const auto& gbuffer =
        render_graph->blackboard().get<DeferredGBufferGraphHandles>();

    render_graph->add_pass<DeferredLightingPassData>(
        "indirect_lighting",
        [&](RenderGraphBuilder& builder, DeferredLightingPassData& data) {
            data.fullscreen_quad = fullscreen_quad_data;
            data.pipeline = pipeline;
            data.gbuffer_set = builder.create_resource_set(
                "deferred.gbuffer",
                pipelines->gbuffer_resource_layout,
                deferred_gbuffer_resource_bindings(
                    gbuffer,
                    pipelines->point_sampler
                )
            );
            if (render_graph->blackboard().contains<VxgiGraphHandles>()) {
                auto& vxgi_handles =
                    render_graph->blackboard().get<VxgiGraphHandles>();
                data.vxgi_set = builder.create_resource_set(
                    "vxgi",
                    vxgi_resources->resource_layout,
                    vxgi_deferred_resource_bindings(
                        *vxgi_resources,
                        vxgi_handles
                    )
                );
            }
            data.target = builder.create_texture(
                "indirect_lighting",
                make_render_texture_desc(
                    std::max<uint32>(1, window->width / 2),
                    std::max<uint32>(1, window->height / 2),
                    PixelFormat::Rgba16Float
                )
            );
            data.target = builder.write_texture(
                data.target,
                RenderGraphAccess::ColorAttachmentWrite
            );
            lighting.indirect = data.target;
        },
        [](RenderGraphContext& context, const DeferredLightingPassData& data) {
            auto gbuffer_set = context.resource_set(data.gbuffer_set);
            auto vxgi_set = context.resource_set(data.vxgi_set);
            auto target = context.texture(data.target);
            execute_fullscreen_lighting_pass(
                context,
                data.fullscreen_quad,
                data.pipeline,
                target,
                [gbuffer_set, vxgi_set](CommandBuffer& command_buffer) {
                    command_buffer.set_resource_set(1, gbuffer_set);
                    command_buffer.set_resource_set(2, vxgi_set);
                }
            );
        }
    );
}

void build_composite_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<RenderTarget> target,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    if (!render_graph->blackboard().contains<DeferredGBufferGraphHandles>() ||
        !render_graph->blackboard().contains<DeferredLightingGraphHandles>()) {
        return;
    }

    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto fullscreen_quad_data = make_fullscreen_quad_pass_data(
        mesh_view_resource_set.resource_set,
        gpu_mesh.value()
    );
    auto pipeline = pipeline_cache->get_render_pipeline(
        pipelines->composite_lighting_pipeline
    );

    auto& lighting =
        render_graph->blackboard().get<DeferredLightingGraphHandles>();
    const auto& gbuffer =
        render_graph->blackboard().get<DeferredGBufferGraphHandles>();
    if (!lighting.direct.is_valid() || !lighting.indirect.is_valid()) {
        return;
    }

    render_graph->add_pass<DeferredCompositePassData>(
        "composite_lighting",
        [&](RenderGraphBuilder& builder, DeferredCompositePassData& data) {
            data.fullscreen_quad = fullscreen_quad_data;
            data.pipeline = pipeline;
            data.gbuffer_set = builder.create_resource_set(
                "deferred.gbuffer",
                pipelines->gbuffer_resource_layout,
                deferred_gbuffer_resource_bindings(
                    gbuffer,
                    pipelines->point_sampler
                )
            );
            data.composite_set = builder.create_resource_set(
                "deferred.composite",
                pipelines->composite_resource_layout,
                deferred_composite_resource_bindings(lighting)
            );
            data.target = builder.import_texture(
                "composite_lighting",
                target->color_texture
            );
            data.target = builder.write_texture(
                data.target,
                RenderGraphAccess::ColorAttachmentWrite
            );
            lighting.composite = data.target;
        },
        [](RenderGraphContext& context, const DeferredCompositePassData& data) {
            auto gbuffer_set = context.resource_set(data.gbuffer_set);
            auto composite_set = context.resource_set(data.composite_set);
            auto target = context.texture(data.target);
            execute_fullscreen_lighting_pass(
                context,
                data.fullscreen_quad,
                data.pipeline,
                target,
                [gbuffer_set, composite_set](CommandBuffer& command_buffer) {
                    command_buffer.set_resource_set(1, gbuffer_set);
                    command_buffer.set_resource_set(2, composite_set);
                }
            );
        }
    );
}

void build_blit_composite_pass(
    ResRW<RenderGraph> render_graph,
    Optional<ResRO<MainSwapchain>> main_swapchain
) {
    if (!render_graph->blackboard().contains<DeferredLightingGraphHandles>()) {
        return;
    }

    if (!main_swapchain || !(*main_swapchain)->swapchain) {
        return;
    }

    const auto composite = render_graph->blackboard()
                               .get<DeferredLightingGraphHandles>()
                               .composite;
    if (!composite.is_valid()) {
        return;
    }

    render_graph->add_pass<BlitCompositePassData>(
        "blit_composite",
        [&](RenderGraphBuilder& builder, BlitCompositePassData& data) {
            data.composite = composite;
            data.target_framebuffer =
                (*main_swapchain)->swapchain->framebuffer();
            builder.read_texture(data.composite, RenderGraphAccess::BlitSource);
            builder.side_effect();
        },
        [](RenderGraphContext& context, const BlitCompositePassData& data) {
            auto composite = context.texture(data.composite);
            auto& command_buffer = context.command_buffer();
            command_buffer.begin_render_pass(
                RenderPassDescription {
                    .color_attachments = {
                        RenderPassColorAttachment {
                            .texture = composite,
                            .load_op = LoadOp::Load,
                        },
                    },
                }
            );
            command_buffer.end_render_pass();
            command_buffer.blit_to(data.target_framebuffer);
        }
    );
}

} // namespace fei
