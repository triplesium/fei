#pragma once

#include "graphics/buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/render_pipeline.hpp"
#include "math/color.hpp"

#include <cstdint>

namespace fei {

struct RenderPassDescriptor {
    Framebuffer* framebuffer {nullptr};
    bool clear_color {false};
    Color4F clear_color_value {0.f, 0.f, 0.f, 0.f};
    bool clear_depth {false};
    float clear_depth_value {0.f};
};

class DrawList {
  public:
    virtual void begin(const RenderPassDescriptor& desc) = 0;
    virtual void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) = 0;
    virtual void bind_render_pipeline(const RenderPipeline* pipeline) = 0;
    virtual void bind_vertex_buffer(const Buffer* buffer) = 0;
    virtual void bind_texture(const Texture2D* texture) = 0;
    virtual void draw(size_t start, size_t count) = 0;
    virtual void end() = 0;
};

} // namespace fei
