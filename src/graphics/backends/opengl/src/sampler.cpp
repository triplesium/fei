#include "graphics_opengl/sampler.hpp"

#include "base/log.hpp"
#include "graphics_opengl/utils.hpp"
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
        default:
            fei::fatal("Unsupported SamplerBorderColor");
            return {};
    }
}

SamplerOpenGL::SamplerOpenGL(const SamplerDescription& desc) : m_desc(desc) {}

void SamplerOpenGL::create_gl_resource() const {
    FEI_GL_CALL(glGenSamplers(1, &m_sampler));

    FEI_GL_CALL(glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_WRAP_S,
        to_gl_address_mode(m_desc.address_mode_u)
    ));
    FEI_GL_CALL(glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_WRAP_T,
        to_gl_address_mode(m_desc.address_mode_v)
    ));
    FEI_GL_CALL(glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_WRAP_R,
        to_gl_address_mode(m_desc.address_mode_w)
    ));

    if (m_desc.address_mode_u == SamplerAddressMode::ClampToBorder ||
        m_desc.address_mode_v == SamplerAddressMode::ClampToBorder ||
        m_desc.address_mode_w == SamplerAddressMode::ClampToBorder) {
        auto border_color = to_color(m_desc.border_color);
        FEI_GL_CALL(glSamplerParameterfv(
            m_sampler,
            GL_TEXTURE_BORDER_COLOR,
            border_color.data()
        ));
    }

    FEI_GL_CALL(
        glSamplerParameterf(m_sampler, GL_TEXTURE_MIN_LOD, m_desc.min_lod)
    );
    FEI_GL_CALL(
        glSamplerParameterf(m_sampler, GL_TEXTURE_MAX_LOD, m_desc.max_lod)
    );
    if (m_desc.lod_bias != 0) {
        FEI_GL_CALL(
            glSamplerParameterf(m_sampler, GL_TEXTURE_LOD_BIAS, m_desc.lod_bias)
        );
    }

    // [TODO] Create two separate samplers for mipmapped and non-mipmapped cases
    bool mipmap = true;
    FEI_GL_CALL(glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_MIN_FILTER,
        to_gl_min_filter(m_desc.min_filter, m_desc.mipmap_filter, mipmap)
    ));
    FEI_GL_CALL(glSamplerParameteri(
        m_sampler,
        GL_TEXTURE_MAG_FILTER,
        to_gl_int(to_gl_mag_filter(m_desc.mag_filter))
    ));

    if (m_desc.comparison_kind.has_value()) {
        FEI_GL_CALL(glSamplerParameteri(
            m_sampler,
            GL_TEXTURE_COMPARE_MODE,
            GL_COMPARE_REF_TO_TEXTURE
        ));
        FEI_GL_CALL(glSamplerParameteri(
            m_sampler,
            GL_TEXTURE_COMPARE_FUNC,
            to_gl_int(to_gl_comparison_function(m_desc.comparison_kind.value()))
        ));
    }
}

void SamplerOpenGL::destroy_gl_resource() {
    if (m_sampler != 0) {
        FEI_GL_CALL(glDeleteSamplers(1, &m_sampler));
        m_sampler = 0;
    }
}

} // namespace fei
