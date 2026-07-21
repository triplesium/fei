#pragma once
#include "app/plugin.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/plugin.hpp"
#include "base/bitflags.hpp"
#include "base/result.hpp"
#include "graphics/enums.hpp"
#include "graphics/sampler.hpp"
#include "graphics/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace fei {

class Image {
  private:
    std::unique_ptr<unsigned char[]> m_data;
    TextureDescription m_texture_description;
    SamplerDescription m_sampler_description;
    std::uint32_t m_channels;

  public:
    Image(
        std::unique_ptr<unsigned char[]> data,
        TextureDescription texture_description,
        std::uint32_t channels = 0,
        SamplerDescription sampler_description = SamplerDescription::Linear
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
    std::uint32_t channels() const { return m_channels; }
    std::uint32_t depth() const { return m_texture_description.depth; }
    const unsigned char* data() const { return m_data.get(); }
    void set_data(std::unique_ptr<unsigned char[]> data) {
        m_data = std::move(data);
    }
    const TextureDescription& texture_description() const {
        return m_texture_description;
    }
    TextureDescription& texture_description() { return m_texture_description; }
    const SamplerDescription& sampler_description() const {
        return m_sampler_description;
    }
    SamplerDescription& sampler_description() { return m_sampler_description; }
};

struct ImageDecodeOptions {
    bool flip_vertically = false;
    bool hdr = false;
    bool srgb = false;
    SamplerDescription sampler = SamplerDescription::Linear;
};

Result<std::unique_ptr<Image>, std::string>
decode_image(std::span<const std::byte> bytes, ImageDecodeOptions options = {});

class ImageLoader : public AssetLoader<Image> {
  public:
    AssetLoadResult<Image>
    load(Reader& reader, const LoadContext& context) override;
};

class ImagePlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_plugins(AssetPlugin<Image, ImageLoader> {});
    }
};

} // namespace fei
