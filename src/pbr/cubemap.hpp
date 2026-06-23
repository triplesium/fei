#pragma once
#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/texture.hpp"
#include "rendering/shader.hpp"

#include <memory>
#include <unordered_map>

namespace fei {

class EquirectToCubemap {
  private:
    std::unordered_map<AssetId, std::shared_ptr<Texture>> m_cubemaps;
    std::shared_ptr<Pipeline> m_equirect_to_cubemap_pipeline;
    std::shared_ptr<ResourceLayout> m_equirect_to_cubemap_resource_layout;

  public:
    void setup(
        const GraphicsDevice& device,
        AssetServer& asset_server,
        Assets<Shader>& shaders
    );

    std::shared_ptr<Texture> convert_equirect_to_cubemap(
        const GraphicsDevice& device,
        std::shared_ptr<Texture> equirect_texture
    );

    Optional<std::shared_ptr<Texture>> get_or_create_cubemap(
        const GraphicsDevice& device,
        Assets<Image>& images,
        Handle<Image> equirect_image_handle
    );
};

class CubemapPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
