#pragma once
#include "graphics/opengl/texture.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/texture_view.hpp"

#include <memory>

namespace fei {

class TextureViewOpenGL : public TextureView {
  private:
    GLuint m_texture_view;
    std::shared_ptr<TextureOpenGL> m_target_gl;
    GLenum m_texture_target;

  public:
    TextureViewOpenGL(const TextureViewDescription& desc);
    ~TextureViewOpenGL();

    GLuint id() const { return m_texture_view; }
    GLenum texture_target() const { return m_texture_target; }
    std::shared_ptr<TextureOpenGL> target() const { return m_target_gl; }
};

} // namespace fei
