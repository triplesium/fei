#pragma once
#include "app/plugin.hpp"
#include "graphics/opengl/device.hpp"

namespace fei {

class OpenGLPlugin : public Plugin {
  public:
    void setup(App& app) { RenderDevice::set_instance(new RenderDeviceOpenGL); }
};

} // namespace fei
