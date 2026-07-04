#pragma once
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "window/window.hpp"

namespace fei {

struct RenderTarget {
    std::shared_ptr<Texture> color_texture;
    std::shared_ptr<Texture> depth_texture;
    std::shared_ptr<Texture> shadow_map_texture;
};

void setup_render_target(
    ResRO<GraphicsDevice> device,
    ResRO<Window> window,
    ResRW<RenderTarget> target
);

} // namespace fei
