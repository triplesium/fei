#pragma once
#include "asset/asset_loader.hpp"

#include <cstdint>
#include <memory>

namespace fei {

class Image {
  private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_channels;
    std::unique_ptr<unsigned char[]> m_data;

  public:
    Image(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t channels,
        std::unique_ptr<unsigned char[]> data
    );

    std::uint32_t width() const { return m_width; }
    std::uint32_t height() const { return m_height; }
    std::uint32_t channels() const { return m_channels; }
    const unsigned char* data() const { return m_data.get(); }
};

class ImageLoader : public AssetLoader<Image> {
  public:
    Image* load(const std::filesystem::path& path) override;
};

} // namespace fei
