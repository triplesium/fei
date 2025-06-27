#pragma once
#include "app/asset.hpp"
#include "app/plugin.hpp"
#include "core/time.hpp"
#include "graphics/opengl/plugin.hpp"
#include "graphics/plugin.hpp"
#include "render2d/render.hpp"
#include "render2d/sprite.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

class SamplePlugin : public fei::Plugin {
  public:
    void setup(fei::App& app) override {
        using namespace fei;
        app.add_plugin<AssetPlugin>()
            .add_plugin<WindowPlugin>()
            .add_plugin<OpenGLPlugin>()
            .add_plugin<SpritePlugin>()
            .add_plugin<RenderPlugin>()
            .add_plugin<TimePlugin>()
            .add_plugin<InputPlugin>()
            .add_plugin<GraphicsPlugin>();
    }
};
