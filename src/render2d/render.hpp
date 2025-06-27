#pragma once

#include "ecs/system_params.hpp"
#include "graphics/device.hpp"
#include "graphics/draw_list.hpp"
#include "graphics/framebuffer.hpp"
#include "window/window.hpp"

namespace fei {

struct RenderResource {
    Framebuffer* framebuffer {nullptr};
    Texture2D* color_tex {nullptr};
    Texture2D* depth_tex {nullptr};
    DrawList* draw_list {nullptr};
    RenderDevice* device {nullptr};
    Color4F clear_color_value {0.2f, 0.3f, 0.3f, 1.0f};
};

void render_setup(Res<RenderResource> render_res, Res<Window> win_res);

void render_start(Res<RenderResource> render_res, Res<Window> win_res);

void render_end(Res<RenderResource> render_res, Res<Window> win_res);

class RenderPlugin : public Plugin {
  public:
    void setup(App& app);
};
} // namespace fei
