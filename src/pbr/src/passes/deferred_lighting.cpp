#include "pbr/passes/deferred_internal.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace fei {

namespace {

struct FullscreenQuadPassData {
    std::shared_ptr<const ResourceSet> view_set;
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    uint32 index_count {};
    uint32 vertex_count {};
};

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
    CommandBuffer& command_buffer,
    const FullscreenQuadPassData& fullscreen_quad,
    std::shared_ptr<Pipeline> pipeline,
    const std::shared_ptr<Texture>& target,
    Draw&& draw
) {
    if (!target) {
        return;
    }
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
    if (pipeline) {
        command_buffer.set_render_pipeline(std::move(pipeline));
        command_buffer.set_resource_set(0, fullscreen_quad.view_set);
        std::forward<Draw>(draw)(command_buffer);
        draw_fullscreen_quad(command_buffer, fullscreen_quad);
    }
    command_buffer.end_render_pass();
}

std::vector<std::shared_ptr<const BindableResource>> gbuffer_bindings(
    const DeferredViewTargets& targets,
    const std::shared_ptr<Sampler>& sampler
) {
    return {
        targets.position_ao,
        targets.normal_roughness,
        targets.albedo_metallic,
        targets.specular,
        targets.emissive_depth,
        sampler,
    };
}

std::shared_ptr<Texture> first_shadow_map(Query<const ShadowMap> query) {
    for (auto [shadow_map] : query) {
        if (shadow_map.texture) {
            return shadow_map.texture;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<const BindableResource>> lighting_bindings(
    const LightingResources& lighting,
    std::shared_ptr<Texture> shadow_map,
    std::shared_ptr<Texture> fallback
) {
    return {
        lighting.uniform_buffer,
        shadow_map ? std::move(shadow_map) : std::move(fallback),
        lighting.shadow_map_sampler,
    };
}

std::vector<std::shared_ptr<const BindableResource>>
vxgi_bindings(const VxgiResources& resources, const VxgiVolumes& volumes) {
    return {
        resources.uniform_buffer,
        volumes.normal,
        volumes.radiance,
        volumes.mipmap[0],
        volumes.mipmap[1],
        volumes.mipmap[2],
        volumes.mipmap[3],
        volumes.mipmap[4],
        volumes.mipmap[5],
        resources.voxel_sampler,
    };
}

} // namespace

void direct_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    Query<const ShadowMap> query_shadow_maps,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device,
    ResRO<DeferredViewTargets> targets,
    ResRO<LightingResources> lighting_resources,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<RenderingDefaults> rendering_defaults
) {
    auto* command_buffer = frame->command_buffer();
    if (!command_buffer || !targets->valid() || query_cameras.empty()) {
        return;
    }
    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto gbuffer_set = resource_sets->get_or_create(
        *device,
        "deferred.gbuffer",
        pipelines->gbuffer_resource_layout,
        gbuffer_bindings(*targets, pipelines->point_sampler)
    );
    auto lighting_set = resource_sets->get_or_create(
        *device,
        "lighting",
        lighting_resources->resource_layout,
        lighting_bindings(
            *lighting_resources,
            first_shadow_map(query_shadow_maps),
            rendering_defaults->default_texture
        )
    );
    auto pipeline = pipeline_cache->get_render_pipeline(
        pipelines->direct_lighting_pipeline
    );
    if (!gbuffer_set || !lighting_set) {
        pipeline.reset();
    }
    auto fullscreen_quad_data = make_fullscreen_quad_pass_data(
        mesh_view_resource_set.resource_set,
        gpu_mesh.value()
    );
    execute_fullscreen_lighting_pass(
        *command_buffer,
        fullscreen_quad_data,
        std::move(pipeline),
        targets->direct,
        [gbuffer_set = std::move(gbuffer_set),
         lighting_set = std::move(lighting_set)](CommandBuffer& commands) {
            commands.set_resource_set(1, gbuffer_set);
            commands.set_resource_set(2, lighting_set);
        }
    );
}

void indirect_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device,
    ResRO<DeferredViewTargets> targets,
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiResources> vxgi_resources,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    auto* command_buffer = frame->command_buffer();
    if (!command_buffer || !targets->valid() || query_cameras.empty()) {
        return;
    }
    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto gbuffer_set = resource_sets->get_or_create(
        *device,
        "deferred.gbuffer",
        pipelines->gbuffer_resource_layout,
        gbuffer_bindings(*targets, pipelines->point_sampler)
    );
    auto vxgi_set = resource_sets->get_or_create(
        *device,
        "vxgi.deferred",
        vxgi_resources->resource_layout,
        vxgi_bindings(*vxgi_resources, *volumes)
    );
    auto pipeline = pipeline_cache->get_render_pipeline(
        pipelines->indirect_lighting_pipeline
    );
    if (!gbuffer_set || !vxgi_set) {
        pipeline.reset();
    }
    auto fullscreen_quad_data = make_fullscreen_quad_pass_data(
        mesh_view_resource_set.resource_set,
        gpu_mesh.value()
    );
    execute_fullscreen_lighting_pass(
        *command_buffer,
        fullscreen_quad_data,
        std::move(pipeline),
        targets->indirect,
        [gbuffer_set = std::move(gbuffer_set),
         vxgi_set = std::move(vxgi_set)](CommandBuffer& commands) {
            commands.set_resource_set(1, gbuffer_set);
            commands.set_resource_set(2, vxgi_set);
        }
    );
}

void composite_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device,
    ResRO<DeferredViewTargets> targets,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    auto* command_buffer = frame->command_buffer();
    if (!command_buffer || !targets->valid() || query_cameras.empty()) {
        return;
    }
    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto gbuffer_set = resource_sets->get_or_create(
        *device,
        "deferred.gbuffer",
        pipelines->gbuffer_resource_layout,
        gbuffer_bindings(*targets, pipelines->point_sampler)
    );
    auto composite_set = resource_sets->get_or_create(
        *device,
        "deferred.composite",
        pipelines->composite_resource_layout,
        {targets->direct, targets->indirect, pipelines->point_sampler}
    );
    auto pipeline = pipeline_cache->get_render_pipeline(
        pipelines->composite_lighting_pipeline
    );
    if (!gbuffer_set || !composite_set) {
        pipeline.reset();
    }
    auto fullscreen_quad_data = make_fullscreen_quad_pass_data(
        mesh_view_resource_set.resource_set,
        gpu_mesh.value()
    );
    execute_fullscreen_lighting_pass(
        *command_buffer,
        fullscreen_quad_data,
        std::move(pipeline),
        targets->composite,
        [gbuffer_set = std::move(gbuffer_set),
         composite_set = std::move(composite_set)](CommandBuffer& commands) {
            commands.set_resource_set(1, gbuffer_set);
            commands.set_resource_set(2, composite_set);
        }
    );
}

void present_composite_pass(
    Optional<ResRO<RenderAssets<GpuMesh>>> gpu_meshes,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device,
    ResRO<DeferredViewTargets> targets,
    Optional<ResRO<DeferredRenderPipelines>> pipelines,
    Optional<ResRO<PipelineCache>> pipeline_cache,
    Optional<ResRO<FullscreenQuad>> fullscreen_quad,
    Optional<ResRO<MainSwapchain>> main_swapchain
) {
    auto* command_buffer = frame->command_buffer();
    if (!command_buffer || !targets->valid() || !main_swapchain ||
        !(*main_swapchain)->swapchain) {
        return;
    }
    if (!gpu_meshes || !pipelines || !pipeline_cache || !fullscreen_quad) {
        return;
    }

    auto gpu_mesh =
        (*gpu_meshes)->get((*fullscreen_quad)->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }
    auto pipeline = (*pipelines)->present_composite_pipeline_requested ?
                        (*pipeline_cache)
                            ->get_render_pipeline(
                                (*pipelines)->present_composite_pipeline
                            ) :
                        nullptr;
    auto present_set = resource_sets->get_or_create(
        *device,
        "deferred.present",
        (*pipelines)->present_resource_layout,
        {targets->composite, (*pipelines)->point_sampler}
    );
    auto fullscreen_quad_data =
        make_fullscreen_quad_pass_data(nullptr, gpu_mesh.value());
    auto target_framebuffer = (*main_swapchain)->swapchain->framebuffer();
    if (!target_framebuffer) {
        return;
    }
    const auto target_width = (*main_swapchain)->swapchain->width();
    const auto target_height = (*main_swapchain)->swapchain->height();

    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
            .framebuffer = target_framebuffer,
        }
    );
    command_buffer->set_viewport(0, 0, target_width, target_height);
    if (pipeline && present_set) {
        command_buffer->set_render_pipeline(std::move(pipeline));
        command_buffer->set_resource_set(0, std::move(present_set));
        draw_fullscreen_quad(*command_buffer, fullscreen_quad_data);
    }
    command_buffer->end_render_pass();
}

} // namespace fei
