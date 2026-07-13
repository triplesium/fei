#include "pbr/passes/target.hpp"

namespace fei {

namespace {

TextureDescription
render_texture_desc(uint32 width, uint32 height, PixelFormat format) {
    return TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = format,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    };
}

} // namespace

void setup_render_target(
    ResRO<GraphicsDevice> device,
    ResRO<Window> window,
    ResRW<RenderTarget> target
) {
    if (window->width <= 0 || window->height <= 0) {
        target->width = 0;
        target->height = 0;
        target->color_texture.reset();
        target->depth_texture.reset();
        return;
    }
    const auto width = static_cast<uint32>(window->width);
    const auto height = static_cast<uint32>(window->height);
    if (target->width == width && target->height == height && target->valid()) {
        return;
    }

    target->width = width;
    target->height = height;
    target->color_texture = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba8Unorm)
    );

    target->depth_texture = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Depth32Float,
            .texture_usage = TextureUsage::DepthStencil,
            .texture_type = TextureType::Texture2D,
        }
    );

    if (!target->shadow_map_texture) {
        target->shadow_map_texture = device->create_texture(
            TextureDescription {
                .width = 2048,
                .height = 2048,
                .depth = 1,
                .mip_level = 1,
                .layer = 1,
                .texture_format = PixelFormat::Depth32Float,
                .texture_usage = TextureUsage::DepthStencil,
                .texture_type = TextureType::Texture2D,
            }
        );
    }
}

void prepare_deferred_view_targets(
    ResRO<GraphicsDevice> device,
    ResRO<Window> window,
    ResRW<DeferredViewTargets> targets
) {
    if (window->width <= 0 || window->height <= 0) {
        *targets = {};
        return;
    }
    const auto width = static_cast<uint32>(window->width);
    const auto height = static_cast<uint32>(window->height);
    if (targets->width == width && targets->height == height &&
        targets->valid()) {
        return;
    }

    targets->width = width;
    targets->height = height;
    targets->position_ao = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba16Float)
    );
    targets->normal_roughness = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba16Float)
    );
    targets->albedo_metallic = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba8Unorm)
    );
    targets->specular = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba8Unorm)
    );
    targets->emissive_depth = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba16Float)
    );
    targets->direct = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba16Float)
    );
    targets->indirect = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba16Float)
    );
    targets->composite = device->create_texture(
        render_texture_desc(width, height, PixelFormat::Rgba8Unorm)
    );
}

} // namespace fei
