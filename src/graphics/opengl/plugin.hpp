#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "graphics/opengl/graphics_device.hpp"

namespace fei {

class OpenGLPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_resource_as<GraphicsDevice>(GraphicsDeviceOpenGL {});
    }
};

} // namespace fei
