#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/server.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh.hpp"
#include "rendering/mesh_loader.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"
#include "rendering/systems.hpp"
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
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_loader<Shader, ShaderLoader>();
    asset_server.add_loader<Mesh, MeshLoader>();
    asset_server.add_without_loader<StandardMaterial>();
    app.add_plugin(RenderAssetPlugin<Image, GpuImage, GpuImageAdapter> {})
        .add_plugin(RenderAssetPlugin<Mesh, GpuMesh, GpuMeshAdapter> {})
        .add_plugin(RenderAssetPlugin<
                    StandardMaterial,
                    PreparedMaterial,
                    PreparedStandardMaterialAdapter> {})
        .add_resource<ViewResource>()
        .add_system(StartUp, init_view_resource)
        .add_system(RenderPrepare, prepare_view_resource)
        .add_resource<MeshUniforms>()
        .add_system(RenderPrepare, prepare_mesh_uniforms)
        .add_system(RenderStart, render_begin)
        .add_system(RenderUpdate, render_mesh)
        .add_system(RenderEnd, render_end);
}

} // namespace fei
