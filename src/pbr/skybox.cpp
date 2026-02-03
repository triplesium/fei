#include "pbr/skybox.hpp"

#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/system_params.hpp"
#include "graphics/shader_module.hpp"
#include "rendering/mesh.hpp"
#include "rendering/shader.hpp"

#include <memory>
#include <vector>

namespace fei {

void setup_skybox_resources(
    Res<GraphicsDevice> device,
    Res<Assets<Mesh>> meshes,
    Res<Assets<Shader>> shaders,
    Res<SkyboxResource> skybox_resource,
    Res<AssetServer> asset_server
) {
    auto mesh = std::make_unique<Mesh>(RenderPrimitive::Triangles);
    std::vector<std::array<float, 3>> positions {
        {-1.0f, 1.0f, -1.0f},  {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},  {1.0f, 1.0f, -1.0f},   {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {-1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f},  {-1.0f, 1.0f, 1.0f},   {-1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f},  {1.0f, -1.0f, 1.0f},   {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {1.0f, 1.0f, -1.0f},   {1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},   {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {1.0f, -1.0f, 1.0f},   {-1.0f, -1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},  {1.0f, 1.0f, -1.0f},   {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {-1.0f, 1.0f, 1.0f},   {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},  {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f}
    };
    mesh->insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
    skybox_resource->mesh = meshes->add(std::move(mesh));
    auto vertex_shader_handle =
        asset_server->load<Shader>("embeded://skybox.vert");
    auto fragment_shader_handle =
        asset_server->load<Shader>("embeded://skybox.frag");
    skybox_resource->shader_modules = {
        device->create_shader_module(ShaderDescription {
            .stage = ShaderStages::Vertex,
            .source = shaders->get(vertex_shader_handle)->source,
        }),
        device->create_shader_module(ShaderDescription {
            .stage = ShaderStages::Fragment,
            .source = shaders->get(fragment_shader_handle)->source,
        })
    };
}

void SkyboxPlugin::setup(App& app) {
    app.add_resource(SkyboxResource {})
        .add_systems(StartUp, setup_skybox_resources);
}

} // namespace fei
