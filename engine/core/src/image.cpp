#include "core/image.hpp"

#include "asset/io.hpp"
#include "graphics/enums.hpp"
#include "graphics/texture.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <cstdint>
#include <stb_image.h>

namespace fei {

namespace {

constexpr std::array<std::byte, 8> image_artifact_magic = {
    std::byte {'F'},
    std::byte {'E'},
    std::byte {'I'},
    std::byte {'I'},
    std::byte {'M'},
    std::byte {'A'},
    std::byte {'G'},
    std::byte {'E'},
};
constexpr std::uint32_t image_artifact_version = 1;

void append_u32(std::vector<std::byte>& output, std::uint32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        output.push_back(
            std::byte {
                static_cast<unsigned char>((value >> (byte * 8U)) & 0xffU),
            }
        );
    }
}

void append_u64(std::vector<std::byte>& output, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        output.push_back(
            std::byte {
                static_cast<unsigned char>((value >> (byte * 8U)) & 0xffU),
            }
        );
    }
}

bool read_u32(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    std::uint32_t& value
) {
    if (bytes.size() - offset < sizeof(value)) {
        return false;
    }
    value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<unsigned char>(bytes[offset + byte])
                 )
                 << (byte * 8U);
    }
    offset += sizeof(value);
    return true;
}

bool read_u64(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    std::uint64_t& value
) {
    if (bytes.size() - offset < sizeof(value)) {
        return false;
    }
    value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<unsigned char>(bytes[offset + byte])
                 )
                 << (byte * 8U);
    }
    offset += sizeof(value);
    return true;
}

bool is_imported_pixel_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8Unorm:
        case PixelFormat::Rg8Unorm:
        case PixelFormat::Rgba8Unorm:
        case PixelFormat::Rgba8UnormSrgb:
        case PixelFormat::Rgba32Float:
            return true;
        default:
            return false;
    }
}

Result<std::size_t, std::string> image_data_size(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t depth,
    PixelFormat format
) {
    const auto pixel_size = get_pixel_format_size(format);
    std::size_t size = width;
    for (const auto factor : {height, depth}) {
        if (factor != 0 &&
            size > std::numeric_limits<std::size_t>::max() / factor) {
            return failure(std::string("Image artifact dimensions overflow"));
        }
        size *= factor;
    }
    if (pixel_size != 0 &&
        size > std::numeric_limits<std::size_t>::max() / pixel_size) {
        return failure(std::string("Image artifact byte size overflows"));
    }
    return size * pixel_size;
}

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
    std::uint32_t channels,
    SamplerDescription sampler_description
) :
    m_data(std::move(data)), m_texture_description(texture_description),
    m_sampler_description(sampler_description),
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
    const auto bytes = std::span(reader.data(), reader.size());
    if (is_image_artifact(bytes)) {
        auto image = decode_image_artifact(bytes);
        if (!image) {
            return failure(image_load_error(context, std::move(image.error())));
        }
        return std::move(*image);
    }
    const auto extension = context.asset_path().path().extension();
    auto image = decode_image(
        bytes,
        ImageDecodeOptions {
            .flip_vertically = extension != ".hdr",
            .hdr = extension == ".hdr",
        }
    );
    if (!image) {
        return failure(image_load_error(context, std::move(image.error())));
    }
    return std::move(*image);
}

std::string_view ImageLoader::artifact_kind() const {
    return "image";
}

Result<std::unique_ptr<Image>, std::string>
decode_image(std::span<const std::byte> bytes, ImageDecodeOptions options) {
    if (bytes.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return failure(std::string("Image data is too large for stb_image"));
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(
            reinterpret_cast<const stbi_uc*>(bytes.data()),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &channels
        )) {
        return failure(stbi_error_message("Failed to read image info"));
    }

    PixelFormat format;
    void* data = nullptr;
    std::uint32_t loaded_channels = 0;
    stbi_set_flip_vertically_on_load_thread(options.flip_vertically);
    if (options.hdr) {
        int req_comp = 4; // Force load as RGBA for HDR
        data = stbi_loadf_from_memory(
            reinterpret_cast<const stbi_uc*>(bytes.data()),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &channels,
            req_comp
        );
        if (!data) {
            return failure(stbi_error_message("Failed to load HDR image"));
        }
        format = PixelFormat::Rgba32Float;
        loaded_channels = static_cast<std::uint32_t>(req_comp);
    } else {
        // For RGB images, load as RGBA, then ignore alpha channel
        int req_comp = channels == 3 ? 4 : channels;
        data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(bytes.data()),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &channels,
            req_comp
        );
        if (!data) {
            return failure(stbi_error_message("Failed to load image"));
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
                format = options.srgb ? PixelFormat::Rgba8UnormSrgb :
                                        PixelFormat::Rgba8Unorm;
                loaded_channels = static_cast<std::uint32_t>(req_comp);
                break;
            default:
                stbi_image_free(data);
                return failure(
                    "Unsupported image channel count: " +
                    std::to_string(channels)
                );
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
        loaded_channels,
        options.sampler
    );
}

bool is_image_artifact(std::span<const std::byte> bytes) {
    return bytes.size() >= image_artifact_magic.size() &&
           std::memcmp(
               bytes.data(),
               image_artifact_magic.data(),
               image_artifact_magic.size()
           ) == 0;
}

Result<std::unique_ptr<Image>, std::string>
decode_image_artifact(std::span<const std::byte> bytes) {
    if (!is_image_artifact(bytes)) {
        return failure(std::string("Invalid image artifact magic"));
    }

    std::size_t offset = image_artifact_magic.size();
    std::uint32_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t channels = 0;
    std::uint32_t format_value = 0;
    std::uint32_t type_value = 0;
    std::uint32_t mip_levels = 0;
    std::uint64_t payload_size = 0;
    if (!read_u32(bytes, offset, version) || !read_u32(bytes, offset, width) ||
        !read_u32(bytes, offset, height) || !read_u32(bytes, offset, depth) ||
        !read_u32(bytes, offset, channels) ||
        !read_u32(bytes, offset, format_value) ||
        !read_u32(bytes, offset, type_value) ||
        !read_u32(bytes, offset, mip_levels) ||
        !read_u64(bytes, offset, payload_size)) {
        return failure(std::string("Image artifact header is truncated"));
    }
    if (version != image_artifact_version) {
        return failure(
            "Unsupported image artifact version: " + std::to_string(version)
        );
    }
    const auto format = static_cast<PixelFormat>(format_value);
    const auto type = static_cast<TextureType>(type_value);
    if (width == 0 || height == 0 || depth == 0 || channels == 0 ||
        channels > 4 || mip_levels != 1 || !is_imported_pixel_format(format) ||
        type_value > static_cast<std::uint32_t>(TextureType::Texture3D)) {
        return failure(std::string("Image artifact header is invalid"));
    }
    auto expected_size = image_data_size(width, height, depth, format);
    if (!expected_size || payload_size != *expected_size ||
        payload_size != bytes.size() - offset) {
        return failure(std::string("Image artifact payload size is invalid"));
    }

    auto data = std::make_unique<unsigned char[]>(*expected_size);
    std::memcpy(data.get(), bytes.data() + offset, *expected_size);
    return std::make_unique<Image>(
        std::move(data),
        TextureDescription {
            .width = width,
            .height = height,
            .depth = depth,
            .mip_level = mip_levels,
            .layer = 1,
            .texture_format = format,
            .texture_usage = TextureUsage::Sampled,
            .texture_type = type,
        },
        channels
    );
}

Status<std::string>
write_image_artifact(const Image& image, const std::filesystem::path& path) {
    const auto& description = image.texture_description();
    if (!image.data() || description.width == 0 || description.height == 0 ||
        description.depth == 0 || description.mip_level != 1 ||
        !is_imported_pixel_format(description.texture_format)) {
        return failure(std::string("Image cannot be encoded as an artifact"));
    }
    auto data_size = image_data_size(
        description.width,
        description.height,
        description.depth,
        description.texture_format
    );
    if (!data_size) {
        return failure(std::move(data_size.error()));
    }

    std::vector<std::byte> output;
    output.reserve(image_artifact_magic.size() + 40 + *data_size);
    output.insert(
        output.end(),
        image_artifact_magic.begin(),
        image_artifact_magic.end()
    );
    append_u32(output, image_artifact_version);
    append_u32(output, description.width);
    append_u32(output, description.height);
    append_u32(output, description.depth);
    append_u32(output, image.channels());
    append_u32(output, static_cast<std::uint32_t>(description.texture_format));
    append_u32(output, static_cast<std::uint32_t>(description.texture_type));
    append_u32(output, description.mip_level);
    append_u64(output, static_cast<std::uint64_t>(*data_size));
    const auto* data = reinterpret_cast<const std::byte*>(image.data());
    output.insert(output.end(), data, data + *data_size);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return failure("Failed to create image artifact: " + path.string());
    }
    stream.write(
        reinterpret_cast<const char*>(output.data()),
        static_cast<std::streamsize>(output.size())
    );
    if (!stream) {
        return failure("Failed to write image artifact: " + path.string());
    }
    return {};
}

} // namespace fei
