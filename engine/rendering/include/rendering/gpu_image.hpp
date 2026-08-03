#pragma once
#include "core/image.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/sampler.hpp"
#include "graphics/texture.hpp"
#include "rendering/render_asset.hpp"

#include <memory>

namespace fei {

class GpuImage {
  private:
    std::shared_ptr<Texture> m_texture;
    std::shared_ptr<Sampler> m_sampler;

  public:
    // [TODO] This is a temporary solution to allow using GpuImage in components
    GpuImage() = default;
    GpuImage(
        std::shared_ptr<Texture> texture,
        std::shared_ptr<Sampler> sampler = nullptr
    ) : m_texture(std::move(texture)), m_sampler(std::move(sampler)) {}

    std::shared_ptr<Texture> texture() { return m_texture; }
    std::shared_ptr<const Texture> texture() const { return m_texture; }
    std::shared_ptr<Sampler> sampler() { return m_sampler; }
    std::shared_ptr<const Sampler> sampler() const { return m_sampler; }
};

class GpuImageAdapter : public RenderAssetAdapter<Image, GpuImage> {
  public:
    Optional<GpuImage>
    prepare_asset(const Image& source_asset, World& world) override {
        auto& device = world.resource<GraphicsDevice>();
        auto texture =
            device.create_texture(source_asset.texture_description());
        if (!texture) {
            return nullopt;
        }
        device.update_texture(
            texture,
            source_asset.data(),
            0,
            0,
            0,
            source_asset.width(),
            source_asset.height(),
            source_asset.depth(),
            0,
            0
        );
        auto sampler =
            device.create_sampler(source_asset.sampler_description());
        if (!sampler) {
            return nullopt;
        }
        return GpuImage {std::move(texture), std::move(sampler)};
    }
};

} // namespace fei
