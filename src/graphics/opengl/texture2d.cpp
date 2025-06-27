#include "graphics/opengl/texture2d.hpp"

namespace fei {

Texture2DOpenGL::Texture2DOpenGL(const TextureDescriptor& desc) :
    m_width(desc.width), m_height(desc.height) {
    convert_pixel_format(
        desc.texture_format,
        m_internal_format,
        m_image_format,
        m_data_type
    );

    m_wrap_s = convert_address_mode(desc.sampler_descriptor.s_address_mode);
    m_wrap_t = convert_address_mode(desc.sampler_descriptor.t_address_mode);

    m_min_filter = convert_filter(desc.sampler_descriptor.min_filter);
    m_mag_filter = convert_filter(desc.sampler_descriptor.mag_filter);

    if (m_texture) {
        glDeleteTextures(1, &m_texture);
    }
    glGenTextures(1, &m_texture);

    update_data(desc.data, desc.width, desc.height);
    opengl_check_error();
}

Texture2DOpenGL::~Texture2DOpenGL() {
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
    }
}

Texture2DOpenGL::Texture2DOpenGL(Texture2DOpenGL&& other) noexcept :
    m_width(other.m_width), m_height(other.m_height),
    m_texture(other.m_texture), m_internal_format(other.m_internal_format),
    m_data_type(other.m_data_type), m_image_format(other.m_image_format),
    m_wrap_s(other.m_wrap_s), m_wrap_t(other.m_wrap_t),
    m_min_filter(other.m_min_filter), m_mag_filter(other.m_mag_filter) {
    // Reset the moved-from object to prevent double deletion
    other.m_texture = 0;
    other.m_width = 0;
    other.m_height = 0;
}

Texture2DOpenGL& Texture2DOpenGL::operator=(Texture2DOpenGL&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (m_texture) {
            glDeleteTextures(1, &m_texture);
        }

        // Move data from other
        m_width = other.m_width;
        m_height = other.m_height;
        m_texture = other.m_texture;
        m_internal_format = other.m_internal_format;
        m_data_type = other.m_data_type;
        m_image_format = other.m_image_format;
        m_wrap_s = other.m_wrap_s;
        m_wrap_t = other.m_wrap_t;
        m_min_filter = other.m_min_filter;
        m_mag_filter = other.m_mag_filter;

        // Reset the moved-from object to prevent double deletion
        other.m_texture = 0;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

void Texture2DOpenGL::update_data(
    const std::byte* data,
    std::uint32_t width,
    std::uint32_t height
) {
    m_width = width;
    m_height = height;

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_wrap_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_mag_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        m_internal_format,
        width,
        height,
        0,
        m_image_format,
        m_data_type,
        data
    );

    opengl_check_error();

    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2DOpenGL::apply(std::uint32_t index) const {
    glActiveTexture(GL_TEXTURE0 + index);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    opengl_check_error();
}

GLuint Texture2DOpenGL::handler() const {
    return m_texture;
}

int Texture2DOpenGL::width() const {
    return m_width;
}

int Texture2DOpenGL::height() const {
    return m_height;
}

} // namespace fei
