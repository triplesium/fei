#pragma once
#include "base/types.hpp"
#include "graphics/texture.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/utils.hpp"

namespace fei {

class TextureOpenGL : public Texture, public DeferredResourceOpenGL {
  private:
    mutable GLuint m_texture {0};
    uint32 m_width, m_height, m_depth;
    uint32 m_mip_level {1};
    uint32 m_layer {1};
    PixelFormat m_texture_format;
    BitFlags<TextureUsage> m_texture_usage;
    TextureType m_texture_type;
    TextureSampleCount m_sample_count {TextureSampleCount::Count1};
    GLenum m_gl_format;
    GLenum m_gl_type;
    GLenum m_gl_sized_internal_format;

  public:
    explicit TextureOpenGL(const TextureDescription& desc);
    TextureOpenGL(const TextureOpenGL&) = delete;

    GLuint id() const { return m_texture; }
    uint32 width() const override { return m_width; }
    uint32 height() const override { return m_height; }
    uint32 depth() const override { return m_depth; }
    uint32 mip_level() const override { return m_mip_level; }
    uint32 layer() const override { return m_layer; }
    PixelFormat format() const override { return m_texture_format; }
    BitFlags<TextureUsage> usage() const override { return m_texture_usage; }
    TextureType type() const override { return m_texture_type; }
    TextureSampleCount sample_count() const override { return m_sample_count; }
    GLenum gl_format() const { return m_gl_format; }
    GLenum gl_type() const { return m_gl_type; }
    GLenum gl_sized_internal_format() const {
        return m_gl_sized_internal_format;
    }

  private:
    void create_gl_resource() const override;
    void destroy_gl_resource() override;
};

} // namespace fei
