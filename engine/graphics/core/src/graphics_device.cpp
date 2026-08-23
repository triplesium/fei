#include "graphics/graphics_device.hpp"

#include <memory>
#include <string>

namespace ets {

namespace {

bool is_capture_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::Rgba8Unorm:
        case PixelFormat::Rgba8UnormSrgb:
        case PixelFormat::Bgra8Unorm:
        case PixelFormat::Bgra8UnormSrgb:
            return true;
        default:
            return false;
    }
}

} // namespace

Result<TextureReadbackFrame, std::string>
GraphicsDevice::capture_presented_frame(const Swapchain& swapchain) const {
    auto framebuffer = swapchain.framebuffer();
    if (!framebuffer || framebuffer->color_attachments().empty()) {
        return failure(
            std::string("The swapchain has no readable color attachment")
        );
    }

    const auto& attachment = framebuffer->color_attachments().front();
    if (!attachment.texture ||
        !is_capture_format(attachment.texture->format())) {
        return failure(
            std::string("The swapchain color format cannot be captured")
        );
    }

    auto readback = create_texture_readback(1);
    if (!readback) {
        return failure(
            std::string("The graphics backend cannot create a texture readback")
        );
    }

    const auto format = attachment.texture->format();
    if (!readback->enqueue(
            TextureReadbackRequest {
                .texture = std::const_pointer_cast<Texture>(attachment.texture),
                .mip_level = attachment.mip_level,
                .layer = attachment.layer,
                .output_format = format,
            }
        )) {
        return failure(
            std::string("The swapchain color attachment cannot be read back")
        );
    }

    auto frame = readback->poll();
    if (!frame) {
        return failure(
            std::string("The swapchain frame readback did not complete")
        );
    }
    return std::move(*frame);
}

} // namespace ets
