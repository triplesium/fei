#pragma once
#include "app/plugin.hpp"
#include "asset/plugin.hpp"
#include "core/plugin.hpp"
#include "graphics/opengl/plugin.hpp"
#include "graphics/plugin.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

class SamplePlugin : public fei::Plugin {
  public:
    void setup(fei::App& app) override {
        using namespace fei;
        app.add_plugin<AssetsPlugin>()
            .add_plugin<WindowPlugin>()
            .add_plugin<OpenGLPlugin>()
            .add_plugin<CorePlugin>()
            .add_plugin<InputPlugin>()
            .add_plugin<GraphicsPlugin>();
    }
};
