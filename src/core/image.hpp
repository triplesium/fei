#pragma once
#include "app/plugin.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/plugin.hpp"
#include "base/bitflags.hpp"
#include "graphics/enums.hpp"
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

    static std::unique_ptr<Image> create_empty(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t depth,
        PixelFormat format,
        BitFlags<TextureUsage> usage,
        TextureType type
    );

    std::uint32_t width() const { return m_texture_description.width; }
    std::uint32_t height() const { return m_texture_description.height; }
    std::uint32_t channels() const { return m_texture_description.depth; }
    std::uint32_t depth() const { return m_texture_description.depth; }
    const unsigned char* data() const { return m_data.get(); }
    void set_data(std::unique_ptr<unsigned char[]> data) {
        m_data = std::move(data);
    }
    const TextureDescription& texture_description() const {
        return m_texture_description;
    }
    TextureDescription& texture_description() { return m_texture_description; }
};

class ImageLoader : public AssetLoader<Image> {
  public:
    std::expected<std::unique_ptr<Image>, std::error_code>
    load(Reader& reader, const LoadContext& context) override;
};

class ImagePlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_plugins(AssetPlugin<Image, ImageLoader> {});
    }
};

} // namespace fei
