#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "rendering/defaults.hpp"
#include "rendering/forward_render.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh.hpp"
#include "rendering/mesh_loader.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"
#include "rendering/view.hpp"
#include "window/window.hpp"

#include <GLFW/glfw3.h>

namespace fei {

void render_begin(Res<GraphicsDevice> device) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->clear_color(Color4F {0.0f, 0.0f, 0.0f, 1.0f});
    command_buffer->clear_depth(1.0f);
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void render_end(Res<Window> win) {
    glfwSwapBuffers(win->glfw_window);
}

void RenderingPlugin::setup(App& app) {
    app.add_plugins(
           AssetPlugin<Shader, ShaderLoader> {},
           AssetPlugin<Mesh, MeshLoader> {},
           AssetPlugin<StandardMaterial> {},
           RenderAssetPlugin<Image, GpuImage, GpuImageAdapter> {},
           RenderAssetPlugin<Mesh, GpuMesh, GpuMeshAdapter> {},
           RenderAssetPlugin<
               StandardMaterial,
               PreparedMaterial,
               PreparedStandardMaterialAdapter> {}
    )
        .add_resource<ViewResource>()
        .add_systems(StartUp, init_view_resource)
        .add_systems(
            RenderPrepare,
            prepare_view_resource,
            prepare_mesh_uniforms
        )
        .add_resource<MeshUniforms>()
        .add_systems(RenderFirst, render_begin)
        .add_systems(RenderLast, render_end)
        .add_plugins(ForwardRenderPlugin {}, RenderingDefaultsPlugin {});
}

} // namespace fei
