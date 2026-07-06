#pragma once

#include "asset/assets.hpp"
#include "asset/server.hpp"
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
#include "rendering/render_graph.hpp"
#include "rendering/shader.hpp"
#include "rendering/visibility.hpp"
#include "window/window.hpp"

namespace fei {

void setup_deferred_pipelines(
    ResRO<GraphicsDevice> device,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Assets<Mesh>> meshes,
    ResRW<Assets<Shader>> shader_assets,
    ResRW<AssetServer> asset_server,
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
);

void build_deferred_prepass(
    ResRW<RenderGraph> render_graph,
    ResRO<DeferredPrepassPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<Window> window,
    ResRO<PipelineCache> pipeline_cache
);

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
);

void build_indirect_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<VxgiResources> vxgi_resources,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Window> window
);

void build_composite_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<RenderTarget> target,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad
);

void build_present_composite_pass(
    Optional<ResRO<RenderAssets<GpuMesh>>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    Optional<ResRO<DeferredRenderPipelines>> pipelines,
    Optional<ResRO<PipelineCache>> pipeline_cache,
    Optional<ResRO<FullscreenQuad>> fullscreen_quad,
    Optional<ResRO<MainSwapchain>> main_swapchain
);

} // namespace fei
