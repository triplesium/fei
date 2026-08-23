#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "graphics_device.hpp"

namespace ets {

ETS_REFLECT(Plugin)
class VulkanPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_resource_as<GraphicsDevice>(GraphicsDeviceVulkan {});
    }
};

} // namespace ets
