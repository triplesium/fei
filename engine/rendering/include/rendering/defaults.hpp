#pragma once
#include "app/plugin.hpp"
#include "graphics/texture.hpp"

#include <memory>

namespace ets {

struct RenderingDefaults {
    // 1x1 white texture
    std::shared_ptr<Texture> default_texture;
};

ETS_REFLECT(Plugin)
class RenderingDefaultsPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ets
