#include "core/image.hpp"

#include "base/log.hpp"
#include "graphics/enums.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <system_error>

#define STB_IMAGE_IMPLEMENTATION
#include <cstdint>
#include <stb_image.h>

namespace fei {

Image::Image(
    std::unique_ptr<unsigned char[]> data,
    TextureDescription texture_description
) : m_data(std::move(data)), m_texture_description(texture_description) {}

std::expected<std::unique_ptr<Image>, std::error_code>
ImageLoader::load(const std::filesystem::path& path) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    auto data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
    if (!data) {
        // TODO: error codes
        return std::unexpected(std::error_code {});
    }
    PixelFormat format;
    switch (channels) {
        case 1:
            format = PixelFormat::R8Unorm;
            break;
        case 2:
            format = PixelFormat::Rg8Unorm;
            break;
        case 3:
            // TODO: Convert RGB to RGBA
            fatal("RGB format not supported, please use RGBA images");
            break;
        case 4:
            format = PixelFormat::Rgba8Unorm;
            break;
        default:
            stbi_image_free(data);
            return std::unexpected(std::error_code {});
    }
    TextureDescription texture_description = TextureDescription {
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .depth = static_cast<std::uint32_t>(channels),
        .mip_level = 1,
        .layer = 0,
        .texture_format = format,
        .texture_usage = TextureUsage::Sampled,
        .texture_type = TextureType::Texture2D,
    };
    return std::make_unique<Image>(
        std::unique_ptr<unsigned char[]>(data),
        texture_description
    );
}

} // namespace fei
