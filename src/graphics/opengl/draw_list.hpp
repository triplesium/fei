#pragma once

#include "graphics/draw_list.hpp"
#include "graphics/opengl/framebuffer.hpp"

#include <cstdint>

namespace fei {

class DrawListOpenGL : public DrawList {
  private:
    const RenderPipeline* m_pipeline;
    FramebufferOpenGL* m_framebuffer;

  public:
    virtual void begin(const RenderPassDescriptor& desc) override;

    virtual void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) override;

    virtual void bind_render_pipeline(const RenderPipeline* pipeline) override;

    virtual void bind_vertex_buffer(const Buffer* buffer) override;

    virtual void bind_texture(const Texture2D* texture) override;

    virtual void draw(size_t start, size_t count) override;

    virtual void end() override;
};

} // namespace fei
