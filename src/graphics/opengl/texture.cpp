#include "graphics/opengl/texture.hpp"
#include "base/log.hpp"
#include "graphics/enums.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

Texture2DOpenGL::Texture2DOpenGL(const TextureDescription& desc) :
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
    m_texture_type = convert_texture_type(desc.texture_type);
    m_mip_level = desc.mip_level;

    glCreateTextures(m_texture_type, 1, &m_texture);

    if (desc.texture_type == TextureType::Texture2D) {
        glTextureStorage2D(
            m_texture,
            m_mip_level,
            m_internal_format,
            m_width,
            m_height
        );
    } else {
        fei::fatal("Unsupported texture type for OpenGL");
        return;
    }

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

// void Texture2DOpenGL::update_data(
//     const std::byte* data,
//     std::uint32_t width,
//     std::uint32_t height
// ) {
//     m_width = width;
//     m_height = height;

//     glBindTexture(GL_TEXTURE_2D, m_texture);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_wrap_s);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_wrap_t);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_min_filter);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_mag_filter);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

//     glTexImage2D(
//         GL_TEXTURE_2D,
//         0,
//         m_internal_format,
//         width,
//         height,
//         0,
//         m_image_format,
//         m_data_type,
//         data
//     );

//     opengl_check_error();

//     glBindTexture(GL_TEXTURE_2D, 0);
// }

} // namespace fei
