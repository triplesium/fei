#include "core/image.hpp"

#include "asset/io.hpp"
#include "graphics/enums.hpp"
#include "graphics/texture.hpp"

#include <cstring>
#include <memory>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include <cstdint>
#include <stb_image.h>

namespace fei {

namespace {

std::uint32_t pixel_format_channels(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8Unorm:
        case PixelFormat::R8Snorm:
        case PixelFormat::R8Uint:
        case PixelFormat::R8Sint:
        case PixelFormat::R16Uint:
        case PixelFormat::R16Sint:
        case PixelFormat::R16Unorm:
        case PixelFormat::R16Snorm:
        case PixelFormat::R16Float:
        case PixelFormat::R32Uint:
        case PixelFormat::R32Sint:
        case PixelFormat::R32Float:
        case PixelFormat::Stencil8:
        case PixelFormat::Depth16Unorm:
        case PixelFormat::Depth24Plus:
        case PixelFormat::Depth32Float:
        case PixelFormat::Bc4RUnorm:
        case PixelFormat::Bc4RSnorm:
        case PixelFormat::EacR11Unorm:
        case PixelFormat::EacR11Snorm:
            return 1;

        case PixelFormat::Rg8Unorm:
        case PixelFormat::Rg8Snorm:
        case PixelFormat::Rg8Uint:
        case PixelFormat::Rg8Sint:
        case PixelFormat::Rg16Uint:
        case PixelFormat::Rg16Sint:
        case PixelFormat::Rg16Unorm:
        case PixelFormat::Rg16Snorm:
        case PixelFormat::Rg16Float:
        case PixelFormat::Rg32Uint:
        case PixelFormat::Rg32Sint:
        case PixelFormat::Rg32Float:
        case PixelFormat::Bc5RgUnorm:
        case PixelFormat::Bc5RgSnorm:
        case PixelFormat::EacRg11Unorm:
        case PixelFormat::EacRg11Snorm:
            return 2;

        case PixelFormat::Rgb9e5Ufloat:
        case PixelFormat::Rg11b10Ufloat:
        case PixelFormat::Bc6hRgbUfloat:
        case PixelFormat::Bc6hRgbFloat:
        case PixelFormat::Etc2Rgb8Unorm:
        case PixelFormat::Etc2Rgb8UnormSrgb:
            return 3;

        default:
            return 4;
    }
}

std::string stbi_error_message(const char* operation) {
    const char* reason = stbi_failure_reason();
    return std::string(operation) + ": " +
           (reason ? reason : "unknown stb_image error");
}

AssetLoadError
image_load_error(const LoadContext& context, std::string message) {
    return AssetLoadError(context.asset_path(), std::move(message));
}

} // namespace

Image::Image(
    std::unique_ptr<unsigned char[]> data,
    TextureDescription texture_description,
    std::uint32_t channels
) :
    m_data(std::move(data)), m_texture_description(texture_description),
    m_channels(
        channels ? channels :
                   pixel_format_channels(texture_description.texture_format)
    ) {}

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
    auto data_size = static_cast<std::size_t>(width) *
                     static_cast<std::size_t>(height) *
                     static_cast<std::size_t>(depth) *
                     get_pixel_format_size(texture_description.texture_format);
    auto data = std::make_unique<unsigned char[]>(data_size);
    return std::make_unique<Image>(
        std::move(data),
        texture_description,
        pixel_format_channels(format)
    );
}

AssetLoadResult<Image>
ImageLoader::load(Reader& reader, const LoadContext& context) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(
            reinterpret_cast<const stbi_uc*>(reader.data()),
            static_cast<int>(reader.size()),
            &width,
            &height,
            &channels
        )) {
        return failure(image_load_error(
            context,
            stbi_error_message("Failed to read image info")
        ));
    }

    PixelFormat format;
    void* data = nullptr;
    std::uint32_t loaded_channels = 0;
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
            return failure(image_load_error(
                context,
                stbi_error_message("Failed to load HDR image")
            ));
        }
        format = PixelFormat::Rgba32Float;
        loaded_channels = static_cast<std::uint32_t>(req_comp);
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
            return failure(image_load_error(
                context,
                stbi_error_message("Failed to load image")
            ));
        }
        switch (channels) {
            case 1:
                format = PixelFormat::R8Unorm;
                loaded_channels = 1;
                break;
            case 2:
                format = PixelFormat::Rg8Unorm;
                loaded_channels = 2;
                break;
            case 3:
            case 4:
                format = PixelFormat::Rgba8Unorm;
                loaded_channels = static_cast<std::uint32_t>(req_comp);
                break;
            default:
                stbi_image_free(data);
                return failure(image_load_error(
                    context,
                    "Unsupported image channel count: " +
                        std::to_string(channels)
                ));
        }
    }
    TextureDescription texture_description = TextureDescription {
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = format,
        .texture_usage = TextureUsage::Sampled,
        .texture_type = TextureType::Texture2D,
    };

    auto data_size = static_cast<std::size_t>(width) *
                     static_cast<std::size_t>(height) *
                     texture_description.depth *
                     get_pixel_format_size(texture_description.texture_format);
    auto image_data = std::make_unique<unsigned char[]>(data_size);
    std::memcpy(image_data.get(), data, data_size);
    stbi_image_free(data);

    return std::make_unique<Image>(
        std::move(image_data),
        texture_description,
        loaded_channels
    );
}

} // namespace fei
