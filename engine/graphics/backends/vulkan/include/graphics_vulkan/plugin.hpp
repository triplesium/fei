#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "graphics_device.hpp"

namespace fei {

class VulkanPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_resource_as<GraphicsDevice>(GraphicsDeviceVulkan {});
    }
};

} // namespace fei
