#pragma once

#include "graphics/enums.hpp"
#include <glad/glad.h>

#include <string>

namespace fei {

std::string opengl_error_string(GLenum const err) noexcept;
bool opengl_check_error();
void convert_pixel_format(
    PixelFormat pixel_format,
    GLint& interal_format,
    GLuint& format,
    GLenum& type
);
GLint convert_address_mode(SamplerAddressMode address_mode);
GLint convert_filter(SamplerFilter filter);
GLenum convert_buffer_type(BufferType type);
GLenum convert_buffer_usage(BufferUsage usage);
GLenum convert_shader_stage(ShaderStage stage);
GLenum convert_render_primitive(RenderPrimitive render_primitive);
GLsizei convert_attribute_size(VertexFormat format);
GLenum convert_attribute_type(VertexFormat format);
GLenum convert_texture_type(TextureType type);

} // namespace fei
