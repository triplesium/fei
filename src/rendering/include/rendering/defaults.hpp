#pragma once
#include "app/plugin.hpp"
#include "graphics/texture.hpp"

#include <memory>

namespace fei {

struct RenderingDefaults {
    // 1x1 white texture
    std::shared_ptr<Texture> default_texture;
};

class RenderingDefaultsPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
