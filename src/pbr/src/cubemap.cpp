#include "pbr/cubemap.hpp"

#include "graphics/enums.hpp"

namespace fei {

std::shared_ptr<Texture> EquirectToCubemap::convert_equirect_to_cubemap(
    const GraphicsDevice& device,
    std::shared_ptr<Texture> equirect_texture
) {
    auto cubemap_texture = device.create_texture(
        TextureDescription {
            .width = 1024,
            .height = 1024,
            .depth = 6,
            .mip_level = 11,
            .layer = 1,
            .texture_format = PixelFormat::Rgba32Float,
            .texture_usage =
                {TextureUsage::Sampled,
                 TextureUsage::Storage,
                 TextureUsage::Cubemap,
                 TextureUsage::GenerateMipmaps},
            .texture_type = TextureType::Texture2D,
        }
    );

    auto resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = m_equirect_to_cubemap_resource_layout,
            .resources =
                {equirect_texture, m_equirect_sampler, cubemap_texture},
            .name = "cubemap.equirect_to_cubemap",
        }
    );

    auto command_buffer = device.create_command_buffer();
    command_buffer->begin();
    command_buffer->set_compute_pipeline(m_equirect_to_cubemap_pipeline);
    command_buffer->set_resource_set(0, resource_set);
    command_buffer->dispatch(
        equirect_texture->width() / 32,
        equirect_texture->height() / 32,
        6
    );
    command_buffer->generate_mipmaps(cubemap_texture);
    command_buffer->end();
    device.submit_commands(command_buffer);

    return cubemap_texture;
}

Optional<std::shared_ptr<Texture>>
EquirectToCubemap::get_cubemap(Handle<Image> equirect_image_handle) const {
    auto it = m_cubemaps.find(equirect_image_handle.id());
    if (it != m_cubemaps.end()) {
        return it->second;
    }
    return nullopt;
}

Optional<std::shared_ptr<Texture>> EquirectToCubemap::prepare_cubemap(
    const GraphicsDevice& device,
    const Assets<Image>& images,
    Handle<Image> equirect_image_handle
) {
    if (auto cubemap = get_cubemap(equirect_image_handle)) {
        return cubemap;
    }
    auto equirect_image = images.get(equirect_image_handle);
    if (!equirect_image) {
        return nullopt;
    }
    auto equirect_texture =
        device.create_texture(equirect_image->texture_description());
    device.update_texture(
        equirect_texture,
        equirect_image->data(),
        0,
        0,
        0,
        equirect_image->width(),
        equirect_image->height(),
        equirect_image->depth(),
        0,
        0
    );
    auto cubemap = convert_equirect_to_cubemap(device, equirect_texture);
    m_cubemaps[equirect_image_handle.id()] = cubemap;
    return cubemap;
}

Optional<std::shared_ptr<Texture>> EquirectToCubemap::get_or_create_cubemap(
    const GraphicsDevice& device,
    const Assets<Image>& images,
    Handle<Image> equirect_image_handle
) {
    return prepare_cubemap(device, images, equirect_image_handle);
}

void EquirectToCubemap::setup(
    const GraphicsDevice& device,
    ShaderCache& shader_cache
) {
    auto compute_shader = shader_cache.get_or_compile(
        AssetPath("shader://equirect2cube.slang"),
        ShaderStages::Compute,
        {}
    );
    m_equirect_to_cubemap_resource_layout = device.create_resource_layout(
        ResourceLayoutDescription {
            .elements = {
                {
                    .binding = 0,
                    .name = "input_texture",
                    .kind = ResourceKind::TextureReadOnly,
                    .stages = {ShaderStages::Compute},
                },
                {
                    .binding = 1,
                    .name = "input_sampler",
                    .kind = ResourceKind::Sampler,
                    .stages = {ShaderStages::Compute},
                },
                {
                    .binding = 2,
                    .name = "output_texture",
                    .kind = ResourceKind::TextureReadWrite,
                    .stages = {ShaderStages::Compute},
                }
            },
        }
    );
    m_equirect_to_cubemap_pipeline = device.create_compute_pipeline(
        ComputePipelineDescription {
            .shader = compute_shader,
            .resource_layouts = {m_equirect_to_cubemap_resource_layout},
        }
    );
    m_equirect_sampler = device.create_sampler(SamplerDescription::Linear);
}

void setup_equi2cubemap(
    ResRW<EquirectToCubemap> ibl,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache
) {
    ibl->setup(*device, *shader_cache);
}

void CubemapPlugin::setup(App& app) {
    app.add_resource(EquirectToCubemap {})
        .add_systems(StartUp, setup_equi2cubemap);
}

} // namespace fei
