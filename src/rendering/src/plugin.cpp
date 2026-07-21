#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "base/log.hpp"
#include "core/transform_plugin.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_profile.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_aabb.hpp"
#include "rendering/mesh/mesh_loader.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/resource_set_cache.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/shader_compiler.hpp"
#include "rendering/view.hpp"
#include "rendering/visibility.hpp"

namespace fei {

namespace {

std::filesystem::path default_shader_cache_root() {
#ifdef FEI_SHADER_CACHE_PATH
    return FEI_SHADER_CACHE_PATH;
#else
    return std::filesystem::current_path() / "build" / "cache" / "shaders";
#endif
}

} // namespace

void flush_graphics_device(ResRW<GraphicsDevice> device) {
    device->flush();
}

void process_pipelines(ResRW<PipelineCache> pipeline_cache) {
    pipeline_cache->process_queued_pipelines();
}

void begin_render_resource_set_cache(ResRW<RenderResourceSetCache> cache) {
    cache->begin_frame();
}

void present_main_swapchain(
    ResRO<GraphicsDevice> device,
    Optional<ResRO<MainSwapchain>> main_swapchain
) {
    if (!main_swapchain) {
        return;
    }
    if (!(*main_swapchain)->swapchain) {
        error("MainSwapchain resource has no swapchain");
        return;
    }

    device->present(*(*main_swapchain)->swapchain);
}

void RenderingPlugin::setup(App& app) {
    if (!app.has_plugin<TransformPlugin>()) {
        app.add_plugin<TransformPlugin>();
    }

    app.resource<AssetServer>().emplace_source<ShaderAssetSource>();
    app.add_resource(SlangLibraryShaderCompiler {});
    app.add_resource(ShaderVariantCompiler(
        app.resource<SlangLibraryShaderCompiler>(),
        RuntimeShaderCompilerConfig {
            .cache_root = default_shader_cache_root(),
        }
    ));

    app.configure_sets(
           RenderUpdate,
           chain(
               RenderingSystems::PrepareAssets(),
               RenderingSystems::PrepareResources(),
               RenderingSystems::CheckVisibility(),
               RenderingSystems::Queue(),
               RenderingSystems::PreparePipelines(),
               RenderingSystems::Render(),
               RenderingSystems::BeginRender(),
               RenderingSystems::Prepass(),
               RenderingSystems::MainPass(),
               RenderingSystems::PostProcess(),
               RenderingSystems::Overlay(),
               RenderingSystems::Submit()
           )
    )
        .add_plugins(
            AssetPlugin<Shader, ShaderLoader> {},
            AssetPlugin<Mesh, MeshLoader> {},
            RenderAssetPlugin<Image, GpuImage, GpuImageAdapter> {},
            RenderAssetPlugin<Mesh, GpuMesh, GpuMeshAdapter> {},
            RenderingDefaultsPlugin {}
        )
        .add_resource(PipelineCache(app.resource<GraphicsDevice>()))
        .add_resource<RenderFrameContext>()
        .add_resource(RenderQueue {})
        .add_resource<RenderResourceSetCache>();

    app.add_resource(ShaderCache(
        app.resource<AssetServer>(),
        app.resource<Assets<Shader>>(),
        app.resource<GraphicsDevice>(),
        &app.resource<ShaderVariantCompiler>()
    ));

    app.add_systems(PostUpdate, compute_mesh_aabb)
        .add_systems(
            RenderUpdate,
            chain(
                init_camera_view_uniform,
                prepare_mesh_uniforms,
                prepare_camera_view_uniform
            ) | in_set<RenderingSystems::PrepareResources>() |
                in_set<RenderingSystems::PrepareView>()
        )
        .add_resource<MeshUniforms>()
        .add_systems(
            RenderUpdate,
            check_mesh_visibility | in_set<RenderingSystems::CheckVisibility>()
        )
        .add_systems(
            RenderUpdate,
            process_pipelines | in_set<RenderingSystems::PreparePipelines>()
        )
        .add_resource<ViewVisibleEntities>()
        .add_systems(
            RenderUpdate,
            chain(
                FEI_NAMED_SYSTEM(begin_render_resource_set_cache),
                FEI_NAMED_SYSTEM(begin_render_frame),
                FEI_NAMED_SYSTEM(flush_render_queue)
            ) | in_set<RenderingSystems::BeginRender>(),
            FEI_NAMED_SYSTEM(submit_render_frame) |
                in_set<RenderingSystems::Submit>()
        )
        .add_systems(RenderLast, present_main_swapchain | main_thread());
}

} // namespace fei
