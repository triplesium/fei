#pragma once
#include "app/plugin.hpp"
#include "asset/plugin.hpp"
#include "core/plugin.hpp"
#include "graphics/plugin.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "input/input.hpp"
#include "window_glfw/input.hpp"

class SamplePlugin : public ets::Plugin {
  public:
    void setup(ets::App& app) override {
        using namespace ets;
        app.add_plugin<AssetsPlugin>()
            .add_plugin<OpenGLGlfwPlugin>()
            .add_plugin<CorePlugin>()
            .add_plugin<GlfwInputPlugin>()
            .add_plugin<GraphicsPlugin>();
    }
};
