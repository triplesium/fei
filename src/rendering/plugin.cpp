#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/mesh.hpp"
#include "rendering/mesh_loader.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/view.hpp"
#include "window/window.hpp"

#include <GLFW/glfw3.h>

namespace fei {

void render_begin(Res<GraphicsDevice> device) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_framebuffer(device->main_framebuffer());
    command_buffer->clear_color(Color4F {0.0f, 0.0f, 0.0f, 1.0f});
    command_buffer->clear_depth(1.0f);
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void render_end(Res<Window> win) {
    glfwSwapBuffers(win->glfw_window);
}

void RenderingPlugin::setup(App& app) {
    app.configure_sets(
           RenderUpdate,
           chain(
               RenderingSystems::PrepareAssets(),
               RenderingSystems::PrepareResources(),
               RenderingSystems::Render()
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
        .add_systems(
            First,
            [](Commands commands,
               Res<AssetServer> asset_server,
               Res<Assets<Shader>> shaders,
               Res<GraphicsDevice> device) {
                commands.add_resource(
                    ShaderCache(*asset_server, *shaders, *device)
                );
            }
        )
        .add_systems(
            RenderUpdate,
            chain(
                init_camera_view_uniform,
                prepare_mesh_uniforms,
                prepare_camera_view_uniform
            ) | in_set<RenderingSystems::PrepareResources>()
        )
        .add_resource<MeshUniforms>()
        .add_systems(RenderFirst, render_begin)
        .add_systems(RenderLast, render_end);
}

} // namespace fei
