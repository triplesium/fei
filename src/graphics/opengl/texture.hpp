#pragma once
#include "base/types.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/texture.hpp"

namespace fei {

class TextureOpenGL : public Texture {
  private:
    GLuint m_texture {0};
    uint32 m_width, m_height, m_depth;
    uint32 m_mip_level {1};
    uint32 m_layer {1};
    PixelFormat m_texture_format;
    BitFlags<TextureUsage> m_texture_usage;
    TextureType m_texture_type;
    GLenum m_gl_format;
    GLenum m_gl_type;
    GLenum m_gl_sized_internal_format;

  public:
    TextureOpenGL(const TextureDescription& desc);
    TextureOpenGL(const TextureOpenGL&) = delete;
    virtual ~TextureOpenGL();

    GLuint id() const { return m_texture; }
    virtual uint32 width() const override { return m_width; }
    virtual uint32 height() const override { return m_height; }
    virtual uint32 depth() const override { return m_depth; }
    virtual uint32 mip_level() const override { return m_mip_level; }
    virtual uint32 layer() const override { return m_layer; }
    virtual PixelFormat format() const override { return m_texture_format; }
    virtual BitFlags<TextureUsage> usage() const override {
        return m_texture_usage;
    }
    virtual TextureType type() const override { return m_texture_type; }
    GLenum gl_format() const { return m_gl_format; }
    GLenum gl_type() const { return m_gl_type; }
    GLenum gl_sized_internal_format() const {
        return m_gl_sized_internal_format;
    }
};

} // namespace fei
