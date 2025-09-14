#include "graphics/opengl/sampler.hpp"

#include "graphics/opengl/utils.hpp"
#include "math/color.hpp"

namespace fei {

inline Color4F to_color(SamplerBorderColor border_color) {
    switch (border_color) {
        case SamplerBorderColor::TransparentBlack:
            return Color4F(0.0f, 0.0f, 0.0f, 0.0f);
        case SamplerBorderColor::OpaqueBlack:
            return Color4F(0.0f, 0.0f, 0.0f, 1.0f);
        case SamplerBorderColor::OpaqueWhite:
            return Color4F(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

SamplerOpenGL::SamplerOpenGL(const SamplerDescription& desc) : m_desc(desc) {
    glGenSamplers(1, &m_sampler);
    opengl_check_error();

    glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_WRAP_S,
        to_gl_address_mode(m_desc.address_mode_u)
    );
    opengl_check_error();
    glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_WRAP_T,
        to_gl_address_mode(m_desc.address_mode_v)
    );
    opengl_check_error();
    glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_WRAP_R,
        to_gl_address_mode(m_desc.address_mode_w)
    );
    opengl_check_error();

    if (desc.address_mode_u == SamplerAddressMode::ClampToBorder ||
        desc.address_mode_v == SamplerAddressMode::ClampToBorder ||
        desc.address_mode_w == SamplerAddressMode::ClampToBorder) {
        auto border_color = to_color(desc.border_color);
        glSamplerParameterfv(
            m_sampler,
            GL_TEXTURE_BORDER_COLOR,
            border_color.data()
        );
        opengl_check_error();
    }

    glSamplerParameterf(m_sampler, GL_TEXTURE_MIN_LOD, m_desc.min_lod);
    opengl_check_error();
    glSamplerParameterf(m_sampler, GL_TEXTURE_MAX_LOD, m_desc.max_lod);
    opengl_check_error();
    if (desc.lod_bias != 0) {
        glSamplerParameterf(m_sampler, GL_TEXTURE_LOD_BIAS, m_desc.lod_bias);
        opengl_check_error();
    }

    // TODO: Support anisotropic filtering & mipmap
    bool mipmap = false;
    glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_MIN_FILTER,
        to_gl_min_filter(m_desc.min_filter, m_desc.mipmap_filter, mipmap)
    );
    opengl_check_error();
    glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_MAG_FILTER,
        to_gl_mag_filter(m_desc.mag_filter)
    );
    opengl_check_error();

    if (m_desc.comparison_kind.has_value()) {
        glSamplerParameteri(
            m_sampler,
            GL_TEXTURE_COMPARE_MODE,
            GL_COMPARE_REF_TO_TEXTURE
        );
        opengl_check_error();
        glSamplerParameteri(
            m_sampler,
            GL_TEXTURE_COMPARE_FUNC,
            to_gl_comparison_function(m_desc.comparison_kind.value())
        );
        opengl_check_error();
    }
}

SamplerOpenGL::~SamplerOpenGL() {
    if (m_sampler) {
        glDeleteSamplers(1, &m_sampler);
        opengl_check_error();
        m_sampler = 0;
    }
}

} // namespace fei
