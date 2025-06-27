#include "graphics/opengl/utils.hpp"
#include "base/log.hpp"

namespace fei {

std::string opengl_error_string(GLenum const err) noexcept {
    switch (err) {
        case GL_NO_ERROR:
            return "GL_NO_ERROR";
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_STACK_OVERFLOW:
            return "GL_STACK_OVERFLOW";
        case GL_STACK_UNDERFLOW:
            return "GL_STACK_UNDERFLOW";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        default:
            return "";
    }
}

bool opengl_check_error() {
    GLenum error = glGetError();
    if (error) {
        auto error_str = opengl_error_string(error);
        fei::error("OpenGL error 0x{:04X}: {}", (unsigned int)error, error_str);
        return true;
    }
    return false;
}

void convert_pixel_format(
    PixelFormat pixel_format,
    GLint& interal_format,
    GLuint& format,
    GLenum& type
) {
    switch (pixel_format) {
        case PixelFormat::RGBA8888: {
            interal_format = GL_RGBA;
            format = GL_RGBA;
            type = GL_UNSIGNED_BYTE;
            break;
        }
        case PixelFormat::RGB888: {
            interal_format = GL_RGB;
            format = GL_RGB;
            type = GL_UNSIGNED_BYTE;
            break;
        }
        case PixelFormat::RGBA4444: {
            interal_format = GL_RGBA;
            format = GL_RGBA;
            type = GL_UNSIGNED_SHORT_4_4_4_4;
            break;
        }
        case PixelFormat::D16: {
            interal_format = GL_DEPTH_COMPONENT;
            format = GL_DEPTH_COMPONENT;
            type = GL_UNSIGNED_INT;
            break;
        }
    }
}

GLint convert_address_mode(SamplerAddressMode address_mode) {
    switch (address_mode) {
        case SamplerAddressMode::Repeat:
            return GL_REPEAT;
        case SamplerAddressMode::MirrorRepeat:
            return GL_MIRRORED_REPEAT;
        case SamplerAddressMode::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
        case SamplerAddressMode::DontCare:
            return GL_REPEAT;
    }
    return 0;
}

GLint convert_filter(SamplerFilter filter) {
    switch (filter) {
        case SamplerFilter::Linear:
            return GL_LINEAR;
        case SamplerFilter::LinearMipmapLinear:
            return GL_LINEAR_MIPMAP_LINEAR;
        case SamplerFilter::LinearMipmapNearest:
            return GL_LINEAR_MIPMAP_NEAREST;
        case SamplerFilter::Nearest:
            return GL_NEAREST;
        case SamplerFilter::NearestMipmapNearest:
            return GL_NEAREST_MIPMAP_NEAREST;
        case SamplerFilter::NearestMipmapLinear:
            return GL_NEAREST_MIPMAP_LINEAR;
        case SamplerFilter::DontCare:
            return GL_NEAREST;
    }
    return 0;
}

GLenum convert_buffer_type(BufferType type) {
    switch (type) {
        case BufferType::Vertex:
            return GL_ARRAY_BUFFER;
        case BufferType::Index:
            return GL_ELEMENT_ARRAY_BUFFER;
    }
    return 0;
}

GLenum convert_buffer_usage(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::Static:
            return GL_STATIC_DRAW;
        case BufferUsage::Dynamic:
            return GL_DYNAMIC_DRAW;
    }
    return 0;
}

GLenum convert_shader_stage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:
            return GL_VERTEX_SHADER;
        case ShaderStage::Fragment:
            return GL_FRAGMENT_SHADER;
    }
    return 0;
}

GLenum convert_render_primitive(RenderPrimitive render_primitive) {
    switch (render_primitive) {
        case RenderPrimitive::Point:
            return GL_POINT;
        case RenderPrimitive::Lines:
            return GL_LINES;
        case RenderPrimitive::LineStrip:
            return GL_LINE_STRIP;
        case RenderPrimitive::Triangles:
            return GL_TRIANGLES;
        case RenderPrimitive::TrianglesStrip:
            return GL_TRIANGLE_STRIP;
    }
    return 0;
}

GLsizei convert_attribute_size(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float4:
        case VertexFormat::Int4:
        case VertexFormat::UByte4:
        case VertexFormat::UShort4:
            return 4;
        case VertexFormat::Float3:
        case VertexFormat::Int3:
            return 3;
        case VertexFormat::Float2:
        case VertexFormat::Int2:
        case VertexFormat::UShort2:
            return 2;
        case VertexFormat::Float:
        case VertexFormat::Int:
            return 1;
    }
    return 0;
}

GLenum convert_attribute_type(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float4:
        case VertexFormat::Float3:
        case VertexFormat::Float2:
        case VertexFormat::Float:
            return GL_FLOAT;
        case VertexFormat::Int4:
        case VertexFormat::Int3:
        case VertexFormat::Int2:
        case VertexFormat::Int:
            return GL_INT;
        case VertexFormat::UShort4:
        case VertexFormat::UShort2:
            return GL_UNSIGNED_SHORT;
        case VertexFormat::UByte4:
            return GL_UNSIGNED_BYTE;
    }
    return 0;
}

} // namespace fei
