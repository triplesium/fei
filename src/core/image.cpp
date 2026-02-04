#include "core/image.hpp"

#include "asset/io.hpp"
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

std::unique_ptr<Image> Image::create_empty(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t depth,
    PixelFormat format,
    BitFlags<TextureUsage> usage,
    TextureType type
) {
    TextureDescription texture_description = TextureDescription {
        .width = width,
        .height = height,
        .depth = depth,
        .mip_level = 1,
        .layer = 1,
        .texture_format = format,
        .texture_usage = usage,
        .texture_type = type,
    };
    auto data_size = width * height * depth *
                     get_pixel_format_size(texture_description.texture_format);
    auto data = std::make_unique<unsigned char[]>(data_size);
    return std::make_unique<Image>(std::move(data), texture_description);
}

std::expected<std::unique_ptr<Image>, std::error_code>
ImageLoader::load(Reader& reader, const LoadContext& context) {
    int width, height, channels;
    stbi_info_from_memory(
        reinterpret_cast<const stbi_uc*>(reader.data()),
        static_cast<int>(reader.size()),
        &width,
        &height,
        &channels
    );
    PixelFormat format;
    void* data = nullptr;
    auto extension = context.asset_path().path().extension();
    if (extension == ".hdr") {
        stbi_set_flip_vertically_on_load(false);
        int req_comp = 4; // Force load as RGBA for HDR
        data = stbi_loadf_from_memory(
            reinterpret_cast<const stbi_uc*>(reader.data()),
            static_cast<int>(reader.size()),
            &width,
            &height,
            &channels,
            req_comp
        );
        if (!data) {
            error("Failed to load HDR image: {}", stbi_failure_reason());
            return std::unexpected(std::error_code {});
        }
        format = PixelFormat::Rgba32Float;
    } else {
        stbi_set_flip_vertically_on_load(true);
        // For RGB images, load as RGBA, then ignore alpha channel
        int req_comp = channels == 3 ? 4 : channels;
        data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(reader.data()),
            static_cast<int>(reader.size()),
            &width,
            &height,
            &channels,
            req_comp
        );
        if (!data) {
            error("Failed to load image: {}", stbi_failure_reason());
            return std::unexpected(std::error_code {});
        }
        switch (channels) {
            case 1:
                format = PixelFormat::R8Unorm;
                break;
            case 2:
                format = PixelFormat::Rg8Unorm;
                break;
            case 3:
            case 4:
                format = PixelFormat::Rgba8Unorm;
                break;
            default:
                stbi_image_free(data);
                return std::unexpected(std::error_code {});
        }
    }
    TextureDescription texture_description = TextureDescription {
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .depth = 1,
        .mip_level = 1,
        .layer = 0,
        .texture_format = format,
        .texture_usage = TextureUsage::Sampled,
        .texture_type = TextureType::Texture2D,
    };
    return std::make_unique<Image>(
        std::unique_ptr<unsigned char[]>(static_cast<unsigned char*>(data)),
        texture_description
    );
}

} // namespace fei
