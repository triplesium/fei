#pragma once
#include "base/bitflags.hpp"
#include "graphics/enums.hpp"

#include <glad/glad.h>
#include <string>

namespace fei {

std::string opengl_error_string(GLenum const err) noexcept;
bool opengl_check_error();
GLint to_gl_address_mode(SamplerAddressMode address_mode);
GLuint to_gl_mag_filter(SamplerFilter mag_filter);
GLint to_gl_min_filter(
    SamplerFilter min_filter,
    SamplerFilter mipmap_filter,
    bool mipmap
);
GLenum to_gl_buffer_target(BitFlags<BufferUsages> usages);
GLenum to_gl_buffer_usage(BitFlags<BufferUsages> usages);
GLenum to_gl_shader_stage(BitFlags<ShaderStages> stages);
GLenum to_gl_render_primitive(RenderPrimitive render_primitive);
GLsizei to_gl_attribute_size(VertexFormat format);
GLenum to_gl_attribute_type(VertexFormat format);
GLenum to_gl_sized_internal_format(PixelFormat format);
GLenum to_gl_texture_target(BitFlags<TextureUsage> usage, TextureType type);
GLenum to_gl_comparison_function(ComparisonKind func);

} // namespace fei
