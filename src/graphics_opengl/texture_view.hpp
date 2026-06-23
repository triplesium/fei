#pragma once
#include "graphics/texture_view.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/texture.hpp"
#include "graphics_opengl/utils.hpp"

#include <memory>

namespace fei {

class TextureViewOpenGL : public TextureView, public DeferredResourceOpenGL {
  private:
    mutable GLuint m_texture_view {0};
    std::shared_ptr<TextureOpenGL> m_target_gl;
    mutable GLenum m_texture_target {0};

  public:
    explicit TextureViewOpenGL(const TextureViewDescription& desc);

    GLuint id() const { return m_texture_view; }
    GLenum texture_target() const { return m_texture_target; }
    std::shared_ptr<TextureOpenGL> target_gl() const { return m_target_gl; }

  private:
    void create_gl_resource() const override;
    void destroy_gl_resource() override;
};

} // namespace fei
