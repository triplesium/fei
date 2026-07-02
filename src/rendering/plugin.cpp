#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "base/log.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_profile.hpp"
#include "graphics/graphics_device.hpp"
#include "profiling/profiling.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_aabb.hpp"
#include "rendering/mesh/mesh_loader.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_graph.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/view.hpp"
#include "rendering/visibility.hpp"
#include "window/window.hpp"

#include <GLFW/glfw3.h>

namespace fei {

void begin_render_graph(ResRW<RenderGraph> render_graph) {
    render_graph->clear();
}

void flush_graphics_device(ResRW<GraphicsDevice> device) {
    device->flush();
}

void process_pipelines(ResRW<PipelineCache> pipeline_cache) {
    pipeline_cache->process_queued_pipelines();
}

void execute_render_graph(
    ResRW<RenderGraph> render_graph,
    ResRO<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    if (!command_buffer) {
        error("GraphicsDevice returned null command buffer for RenderGraph");
        return;
    }

    command_buffer->begin();
    if (render_graph->compile()) {
        render_graph->execute(*device, *command_buffer);
    } else {
        error("RenderGraph compile failed: {}", render_graph->compile_error());
    }
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void render_end(ResRO<Window> win) {
    FEI_PROFILE_SCOPE("Swap Buffers");
    glfwSwapBuffers(win->glfw_window);
}

void RenderingPlugin::setup(App& app) {
    app.resource<AssetServer>().emplace_source<ShaderAssetSource>();

    app.configure_sets(
           RenderUpdate,
           chain(
               RenderingSystems::PrepareAssets(),
               RenderingSystems::PrepareResources(),
               RenderingSystems::CheckVisibility(),
               RenderingSystems::Queue(),
               RenderingSystems::PreparePipelines(),
               RenderingSystems::BuildRenderGraph(),
               RenderingSystems::Render(),
               RenderingSystems::ExecuteRenderGraph()
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
        .add_resource<RenderGraph>()
        .add_systems(
            First,
            FEI_SYSTEM_NAME(
                "init_shader_cache",
                [](Commands commands,
                   ResRW<AssetServer> asset_server,
                   ResRW<Assets<Shader>> shaders,
                   ResRO<GraphicsDevice> device) {
                    commands.add_resource(
                        ShaderCache(*asset_server, *shaders, *device)
                    );
                }
            )
        )
        .add_systems(PostUpdate, compute_mesh_aabb)
        .add_systems(
            RenderUpdate,
            chain(
                init_camera_view_uniform,
                prepare_mesh_uniforms,
                prepare_camera_view_uniform
            ) | in_set<RenderingSystems::PrepareResources>()
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
        .add_systems(RenderFirst, begin_render_graph)
        .add_systems(
            RenderUpdate,
            execute_render_graph |
                in_set<RenderingSystems::ExecuteRenderGraph>()
        )
        .add_systems(RenderLast, chain(flush_graphics_device, render_end));
}

} // namespace fei
