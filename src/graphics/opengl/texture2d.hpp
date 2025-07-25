#pragma once

#include "graphics/opengl/utils.hpp"
#include "graphics/texture2d.hpp"

#include <cstdint>

namespace fei {

class Texture2DOpenGL : public Texture2D {
  private:
    int m_width, m_height;
    GLuint m_texture {0};
    GLint m_internal_format;
    GLenum m_data_type;
    GLuint m_image_format;
    GLint m_wrap_s, m_wrap_t;
    GLint m_min_filter, m_mag_filter;

  public:
    Texture2DOpenGL(const TextureDescriptor& desc);
    Texture2DOpenGL(Texture2DOpenGL&&) noexcept;
    Texture2DOpenGL& operator=(Texture2DOpenGL&&) noexcept;
    Texture2DOpenGL(const Texture2DOpenGL&) = delete;
    Texture2DOpenGL& operator=(const Texture2DOpenGL&) = delete;

    ~Texture2DOpenGL();

    void update_data(
        const std::byte* data,
        std::uint32_t width,
        std::uint32_t height
    ) override;

    void apply(std::uint32_t index) const override;

    GLuint handler() const;

    int width() const override;
    int height() const override;
};

} // namespace fei
