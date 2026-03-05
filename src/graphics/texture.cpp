#include "graphics/texture.hpp"

#include "graphics/graphics_device.hpp"

#include <memory>

namespace fei {

std::shared_ptr<TextureView> Texture::full_view(GraphicsDevice& device) {
    if (!m_full_view) {
        m_full_view = device.create_texture_view(TextureViewDescription {
            .target = shared_from_this(),
            .base_mip_level = 0,
            .mip_levels = mip_level(),
            .base_array_layer = 0,
            .array_layers = layer(),
            .format = format()
        });
    }
    return m_full_view;
}
} // namespace fei
