#pragma once

#include "base/types.hpp"
#include "graphics/enums.hpp"

#include <memory>

namespace ets {

class Framebuffer;
class GraphicsDevice;
struct MainSwapchain;
class Texture;

enum class SpriteOutputMode {
    MainSwapchain,
    Texture,
};

struct SpriteOutput {
    SpriteOutputMode mode {SpriteOutputMode::MainSwapchain};
    uint32 requested_width {1};
    uint32 requested_height {1};
    PixelFormat texture_format {PixelFormat::Rgba8Unorm};

    uint32 width {0};
    uint32 height {0};
    std::shared_ptr<Texture> texture;
    std::shared_ptr<const Framebuffer> framebuffer;

    void resize(uint32 new_width, uint32 new_height) {
        requested_width = new_width;
        requested_height = new_height;
    }
};

void update_sprite_output(
    const GraphicsDevice& device,
    const MainSwapchain* main_swapchain,
    SpriteOutput& output
);

} // namespace ets
