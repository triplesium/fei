#pragma once
#include "asset/handle.hpp"
#include "asset/id.hpp"
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
    std::unordered_map<AssetId, std::shared_ptr<Texture>> cubemaps;
    std::shared_ptr<Pipeline> equirect_to_cubemap_pipeline;
    std::shared_ptr<ResourceLayout> equirect_to_cubemap_resource_layout;

  public:
    void setup(
        GraphicsDevice& device,
        AssetServer& asset_server,
        Assets<Shader>& shaders
    );

    std::shared_ptr<Texture> convert_equirect_to_cubemap(
        GraphicsDevice& device,
        std::shared_ptr<Texture> equirect_texture
    );

    std::shared_ptr<Texture> get_or_create_cubemap(
        GraphicsDevice& device,
        Assets<Image>& images,
        Handle<Image> equirect_image_handle
    );
};

class CubemapPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
