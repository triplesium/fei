#include "sprite/output.hpp"

#include "graphics/framebuffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "graphics/texture.hpp"

#include <utility>

namespace ets {

namespace {

void clear_output(SpriteOutput& output) {
    output.width = 0;
    output.height = 0;
    output.texture.reset();
    output.framebuffer.reset();
}

} // namespace

void update_sprite_output(
    const GraphicsDevice& device,
    const MainSwapchain* main_swapchain,
    SpriteOutput& output
) {
    if (output.mode == SpriteOutputMode::MainSwapchain) {
        output.texture.reset();
        output.framebuffer.reset();
        if (main_swapchain == nullptr || !main_swapchain->swapchain) {
            clear_output(output);
            return;
        }

        output.width = main_swapchain->swapchain->width();
        output.height = main_swapchain->swapchain->height();
        output.framebuffer = main_swapchain->swapchain->framebuffer();
        return;
    }

    if (output.requested_width == 0 || output.requested_height == 0) {
        clear_output(output);
        return;
    }
    if (output.texture && output.framebuffer &&
        output.width == output.requested_width &&
        output.height == output.requested_height &&
        output.texture->format() == output.texture_format) {
        return;
    }

    auto texture = device.create_texture(
        TextureDescription {
            .width = output.requested_width,
            .height = output.requested_height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = output.texture_format,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );
    if (!texture) {
        clear_output(output);
        return;
    }

    auto framebuffer = device.create_framebuffer(
        FramebufferDescription {
            .color_targets = {FramebufferAttachment {.texture = texture}},
        }
    );
    if (!framebuffer) {
        clear_output(output);
        return;
    }

    output.width = output.requested_width;
    output.height = output.requested_height;
    output.texture = std::move(texture);
    output.framebuffer = std::move(framebuffer);
}

} // namespace ets
