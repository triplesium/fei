#pragma once
#include "app/plugin.hpp"
#include "asset/loader.hpp"
#include "asset/server.hpp"
#include "graphics/texture.hpp"

#include <cstdint>
#include <expected>
#include <memory>

namespace fei {

class Image {
  private:
    std::unique_ptr<unsigned char[]> m_data;
    TextureDescription m_texture_description;

  public:
    Image(
        std::unique_ptr<unsigned char[]> data,
        TextureDescription texture_description
    );

    std::uint32_t width() const { return m_texture_description.width; }
    std::uint32_t height() const { return m_texture_description.height; }
    std::uint32_t channels() const { return m_texture_description.depth; }
    const unsigned char* data() const { return m_data.get(); }
    const TextureDescription& texture_description() const {
        return m_texture_description;
    }
};

class ImageLoader : public AssetLoader<Image> {
  public:
    std::expected<std::unique_ptr<Image>, std::error_code>
    load(const std::filesystem::path& path) override;
};

class ImagePlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.resource<AssetServer>().add_loader<Image, ImageLoader>();
    }
};

} // namespace fei
