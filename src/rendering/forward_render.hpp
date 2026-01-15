#pragma once
#include "app/plugin.hpp"
#include "graphics/texture.hpp"

#include <memory>

namespace fei {

struct ForwardRenderResources {
    std::shared_ptr<Texture> color_texture;
    std::shared_ptr<Texture> depth_texture;
};

class ForwardRenderPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
