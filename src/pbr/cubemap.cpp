#include "pbr/cubemap.hpp"

namespace fei {

std::shared_ptr<Texture> EquirectToCubemap::convert_equirect_to_cubemap(
    GraphicsDevice& device,
    std::shared_ptr<Texture> equirect_texture
) {
    auto cubemap_texture = device.create_texture(TextureDescription {
        .width = 1024,
        .height = 1024,
        .depth = 6,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba32Float,
        .texture_usage =
            {
                TextureUsage::Sampled,
                TextureUsage::Storage,
                TextureUsage::Cubemap,
            },
    });

    auto resource_set = device.create_resource_set(ResourceSetDescription {
        .layout = equirect_to_cubemap_resource_layout,
        .resources = {equirect_texture, cubemap_texture},
    });

    auto command_buffer = device.create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(equirect_to_cubemap_pipeline);
    command_buffer->set_resource_set(0, resource_set);
    command_buffer->dispatch(
        equirect_texture->width() / 32,
        equirect_texture->height() / 32,
        6
    );
    command_buffer->end();
    device.submit_commands(command_buffer);

    return cubemap_texture;
}

std::shared_ptr<Texture> EquirectToCubemap::get_or_create_cubemap(
    GraphicsDevice& device,
    Assets<Image>& images,
    Handle<Image> equirect_image_handle
) {
    auto it = cubemaps.find(equirect_image_handle.id());
    if (it != cubemaps.end()) {
        return it->second;
    }
    auto& equirect_image = images.get(equirect_image_handle).value();
    auto equirect_texture =
        device.create_texture(equirect_image.texture_description());
    device.update_texture(
        equirect_texture,
        equirect_image.data(),
        0,
        0,
        0,
        equirect_image.width(),
        equirect_image.height(),
        equirect_image.channels(),
        0,
        0
    );
    auto cubemap = convert_equirect_to_cubemap(device, equirect_texture);
    cubemaps[equirect_image_handle.id()] = cubemap;
    return cubemap;
}

void EquirectToCubemap::setup(
    GraphicsDevice& device,
    AssetServer& asset_server,
    Assets<Shader>& shaders
) {
    auto shader_handle =
        asset_server.load<Shader>("embeded://equirect2cube.comp");
    auto& shader = shaders.get(shader_handle).value();
    auto compute_shader = device.create_shader_module(shader.description());
    equirect_to_cubemap_resource_layout =
        device.create_resource_layout(ResourceLayoutDescription {
            .elements =
                {{
                     .binding = 0,
                     .name = "input_texture",
                     .kind = ResourceKind::TextureReadOnly,
                     .stages = {ShaderStages::Compute},
                 },
                 {
                     .binding = 1,
                     .name = "output_texture",
                     .kind = ResourceKind::TextureReadWrite,
                     .stages = {ShaderStages::Compute},
                 }},
        });
    equirect_to_cubemap_pipeline =
        device.create_compute_pipeline(ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {equirect_to_cubemap_resource_layout},
        });
}

void setup_equi2cubemap(
    Res<EquirectToCubemap> ibl,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shaders
) {
    ibl->setup(*device, *asset_server, *shaders);
}

void CubemapPlugin::setup(App& app) {
    app.add_resource(EquirectToCubemap {})
        .add_systems(StartUp, setup_equi2cubemap);
}

} // namespace fei
