#pragma once
#include "app/plugin.hpp"
#include "graphics/buffer.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <vector>

namespace fei {

struct ForwardRenderResources {
    std::shared_ptr<Texture> color_texture;
    std::shared_ptr<Texture> depth_texture;

    // Shadow related resources
    std::shared_ptr<Texture> shadow_map_texture;
    std::vector<std::shared_ptr<ShaderModule>> shadow_shader_modules;
    std::shared_ptr<Buffer> shadow_uniform_buffer;
    std::shared_ptr<ResourceSet> shadow_resource_set;
};

class ForwardRenderPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
