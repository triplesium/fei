#include "core/image.hpp"

#include "base/log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <cstdint>
#include <stb_image.h>

namespace fei {

Image::Image(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t channels,
    std::unique_ptr<unsigned char[]> data
) :
    m_width(width), m_height(height), m_channels(channels),
    m_data(std::move(data)) {}

Image* ImageLoader::load(const std::filesystem::path& path) {
    int width, height, channels;
    auto data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
    if (!data) {
        fei::error("Failed to load image");
        return nullptr;
    }
    return new Image(
        width,
        height,
        channels,
        std::unique_ptr<unsigned char[]>(data)
    );
}

} // namespace fei
