#pragma once

#include "graphics/opengl/utils.hpp"
#include "graphics/texture.hpp"

namespace fei {

class Texture2DOpenGL : public Texture {
  private:
    int m_width, m_height;
    GLuint m_texture {0};
    GLint m_internal_format;
    GLenum m_data_type;
    GLuint m_image_format;
    GLint m_wrap_s, m_wrap_t;
    GLint m_min_filter, m_mag_filter;
    GLenum m_texture_type;
    GLsizei m_mip_level;

  public:
    Texture2DOpenGL(const TextureDescription& desc);
    Texture2DOpenGL(Texture2DOpenGL&&) noexcept;
    Texture2DOpenGL& operator=(Texture2DOpenGL&&) noexcept;
    Texture2DOpenGL(const Texture2DOpenGL&) = delete;
    Texture2DOpenGL& operator=(const Texture2DOpenGL&) = delete;
    ~Texture2DOpenGL();

    GLuint id() const { return m_texture; }
    int width() const override { return m_width; }
    int height() const override { return m_height; }
};

} // namespace fei
