#include "pbr/passes/target.hpp"

namespace fei {

void setup_render_target(
    Res<GraphicsDevice> device,
    Res<Window> window,
    Res<RenderTarget> target
) {
    uint32 width = window->width;
    uint32 height = window->height;
    target->color_texture = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 3,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = TextureUsage::RenderTarget,
        .texture_type = TextureType::Texture2D,
    });

    target->depth_texture = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Depth32Float,
        .texture_usage = TextureUsage::DepthStencil,
        .texture_type = TextureType::Texture2D,
    });

    target->shadow_map_texture = device->create_texture(TextureDescription {
        .width = 2048,
        .height = 2048,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Depth32Float,
        .texture_usage = TextureUsage::DepthStencil,
        .texture_type = TextureType::Texture2D,
    });
}

} // namespace fei
