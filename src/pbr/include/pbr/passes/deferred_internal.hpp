#pragma once

#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/passes/deferred.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/pipelines.hpp"
#include "pbr/postprocess.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/components.hpp"
#include "rendering/defaults.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/resource_set_cache.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/visibility.hpp"
namespace fei {

void setup_deferred_pipelines(
    ResRO<GraphicsDevice> device,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Assets<Mesh>> meshes,
    ResRW<ShaderCache> shader_cache,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<LightingResources> lighting_resources,
    ResRO<VxgiResources> vxgi_resources,
    ResRW<DeferredRenderPipelines> pipelines,
    ResRW<PipelineCache> pipeline_cache,
    Optional<ResRO<MainSwapchain>> main_swapchain
);

void queue_deferred_prepass_meshes(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const GlobalTransform3d> query_meshes,
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
);

void deferred_prepass(
    ResRW<RenderFrameContext> frame,
    ResRO<DeferredPrepassPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<DeferredViewTargets> targets,
    ResRO<PipelineCache> pipeline_cache
);

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
);

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
);

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
);

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
);

} // namespace fei
