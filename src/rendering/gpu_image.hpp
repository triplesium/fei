#pragma once
#include "core/image.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "rendering/render_asset.hpp"

#include <memory>

namespace fei {

class GpuImage {
  private:
    std::shared_ptr<Texture> m_texture;

  public:
    // [TODO] This is a temporary solution to allow using GpuImage in components
    GpuImage() = default;
    GpuImage(std::shared_ptr<Texture> texture) :
        m_texture(std::move(texture)) {}

    std::shared_ptr<Texture> texture() const { return m_texture; }
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
        return {texture};
    }
};

} // namespace fei
