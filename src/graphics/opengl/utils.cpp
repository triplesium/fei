#include "graphics/opengl/utils.hpp"

#include "base/log.hpp"
#include "graphics/enums.hpp"

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

GLint to_gl_address_mode(SamplerAddressMode address_mode) {
    switch (address_mode) {
        case SamplerAddressMode::Repeat:
            return GL_REPEAT;
        case SamplerAddressMode::MirrorRepeat:
            return GL_MIRRORED_REPEAT;
        case SamplerAddressMode::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
        case SamplerAddressMode::ClampToBorder:
            return GL_CLAMP_TO_BORDER;
    }
}

GLuint to_gl_mag_filter(SamplerFilter mag_filter) {
    switch (mag_filter) {
        case SamplerFilter::Nearest:
            return GL_NEAREST;
        case SamplerFilter::Linear:
            return GL_LINEAR;
    }
}

GLint to_gl_min_filter(
    SamplerFilter min_filter,
    SamplerFilter mipmap_filter,
    bool mipmap
) {
    if (min_filter == SamplerFilter::Linear) {
        if (mipmap) {
            if (mipmap_filter == SamplerFilter::Linear) {
                return GL_LINEAR_MIPMAP_LINEAR;
            } else {
                return GL_LINEAR_MIPMAP_NEAREST;
            }
        } else {
            return GL_LINEAR;
        }
    } else {
        if (mipmap) {
            if (mipmap_filter == SamplerFilter::Linear) {
                return GL_NEAREST_MIPMAP_LINEAR;
            } else {
                return GL_NEAREST_MIPMAP_NEAREST;
            }
        } else {
            return GL_NEAREST;
        }
    }
}

GLenum convert_buffer_type(BitFlags<BufferUsages> usages) {
    if (usages.is_set(BufferUsages::Vertex)) {
        return GL_ARRAY_BUFFER;
    }
    if (usages.is_set(BufferUsages::Index)) {
        return GL_ELEMENT_ARRAY_BUFFER;
    }
    return 0;
}

GLenum to_gl_buffer_usage(BitFlags<BufferUsages> usages) {
    if (usages.is_set(BufferUsages::Dynamic)) {
        return GL_DYNAMIC_DRAW;
    }
    return GL_STATIC_DRAW;
}

GLenum to_gl_shader_stage(BitFlags<ShaderStages> stages) {
    if (stages.is_set(ShaderStages::Vertex)) {
        return GL_VERTEX_SHADER;
    }
    if (stages.is_set(ShaderStages::Fragment)) {
        return GL_FRAGMENT_SHADER;
    }
    if (stages.is_set(ShaderStages::Compute)) {
        return GL_COMPUTE_SHADER;
    }
    return 0;
}

GLenum to_gl_render_primitive(RenderPrimitive render_primitive) {
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

GLsizei to_gl_attribute_size(VertexFormat format) {
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

GLenum to_gl_attribute_type(VertexFormat format) {
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

GLenum to_gl_sized_internal_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8Unorm:
            return GL_R8;
        case PixelFormat::R8Snorm:
            return GL_R8_SNORM;
        case PixelFormat::R8Uint:
            return GL_R8UI;
        case PixelFormat::R8Sint:
            return GL_R8I;
        case PixelFormat::R16Uint:
            return GL_R16UI;
        case PixelFormat::R16Sint:
            return GL_R16I;
        case PixelFormat::R16Unorm:
            return GL_R16;
        case PixelFormat::R16Snorm:
            return GL_R16_SNORM;
        case PixelFormat::R16Float:
            return GL_R16F;
        case PixelFormat::Rg8Unorm:
            return GL_RG8;
        case PixelFormat::Rg8Snorm:
            return GL_RG8_SNORM;
        case PixelFormat::Rg8Uint:
            return GL_RG8UI;
        case PixelFormat::Rg8Sint:
            return GL_RG8I;
        case PixelFormat::R32Uint:
            return GL_R32UI;
        case PixelFormat::R32Sint:
            return GL_R32I;
        case PixelFormat::R32Float:
            return GL_R32F;
        case PixelFormat::Rg16Uint:
            return GL_RG16UI;
        case PixelFormat::Rg16Sint:
            return GL_RG16I;
        case PixelFormat::Rg16Unorm:
            return GL_RG16;
        case PixelFormat::Rg16Snorm:
            return GL_RG16_SNORM;
        case PixelFormat::Rg16Float:
            return GL_RG16F;
        case PixelFormat::Rgba8Unorm:
            return GL_RGBA8;
        case PixelFormat::Rgba8UnormSrgb:
            return GL_SRGB8_ALPHA8;
        case PixelFormat::Rgba8Snorm:
            return GL_RGBA8_SNORM;
        case PixelFormat::Rgba8Uint:
            return GL_RGBA8UI;
        case PixelFormat::Rgba8Sint:
            return GL_RGBA8I;
        case PixelFormat::Bgra8Unorm:
            return GL_RGBA8;
        case PixelFormat::Bgra8UnormSrgb:
            return GL_SRGB8_ALPHA8;
        case PixelFormat::Rgb9e5Ufloat:
            return GL_RGB9_E5;
        case PixelFormat::Rgb10a2Uint:
            return GL_RGB10_A2UI;
        case PixelFormat::Rgb10a2Unorm:
            return GL_RGB10_A2;
        case PixelFormat::Rg11b10Ufloat:
            return GL_R11F_G11F_B10F;
        case PixelFormat::Rg32Uint:
            return GL_RG32UI;
        case PixelFormat::Rg32Sint:
            return GL_RG32I;
        case PixelFormat::Rg32Float:
            return GL_RG32F;
        case PixelFormat::Rgba16Uint:
            return GL_RGBA16UI;
        case PixelFormat::Rgba16Sint:
            return GL_RGBA16I;
        case PixelFormat::Rgba16Unorm:
            return GL_RGBA16;
        case PixelFormat::Rgba16Snorm:
            return GL_RGBA16_SNORM;
        case PixelFormat::Rgba16Float:
            return GL_RGBA16F;
        case PixelFormat::Rgba32Uint:
            return GL_RGBA32UI;
        case PixelFormat::Rgba32Sint:
            return GL_RGBA32I;
        case PixelFormat::Rgba32Float:
            return GL_RGBA32F;
        case PixelFormat::Stencil8:
            return GL_STENCIL_INDEX8;
        case PixelFormat::Depth16Unorm:
            return GL_DEPTH_COMPONENT16;
        case PixelFormat::Depth24Plus:
            return GL_DEPTH_COMPONENT24;
        case PixelFormat::Depth24PlusStencil8:
            return GL_DEPTH24_STENCIL8;
        case PixelFormat::Depth32Float:
            return GL_DEPTH_COMPONENT32F;
        case PixelFormat::Depth32FloatStencil8:
            return GL_DEPTH32F_STENCIL8;
        // case PixelFormat::Bc1RgbaUnorm:
        //     return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        // case PixelFormat::Bc1RgbaUnormSrgb:
        //     return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
        // case PixelFormat::Bc2RgbaUnorm:
        //     return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        // case PixelFormat::Bc2RgbaUnormSrgb:
        //     return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
        // case PixelFormat::Bc3RgbaUnorm:
        //     return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        // case PixelFormat::Bc3RgbaUnormSrgb:
        //     return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
        case PixelFormat::Bc4RUnorm:
            return GL_COMPRESSED_RED_RGTC1;
        case PixelFormat::Bc4RSnorm:
            return GL_COMPRESSED_SIGNED_RED_RGTC1;
        case PixelFormat::Bc5RgUnorm:
            return GL_COMPRESSED_RG_RGTC2;
        case PixelFormat::Bc5RgSnorm:
            return GL_COMPRESSED_SIGNED_RG_RGTC2;
        case PixelFormat::Bc6hRgbUfloat:
            return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
        case PixelFormat::Bc6hRgbFloat:
            return GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;
        case PixelFormat::Bc7RgbaUnorm:
            return GL_COMPRESSED_RGBA_BPTC_UNORM;
        case PixelFormat::Bc7RgbaUnormSrgb:
            return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
        case PixelFormat::Etc2Rgb8Unorm:
            return GL_COMPRESSED_RGB8_ETC2;
        case PixelFormat::Etc2Rgb8UnormSrgb:
            return GL_COMPRESSED_SRGB8_ETC2;
        case PixelFormat::Etc2Rgb8A1Unorm:
            return GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        case PixelFormat::Etc2Rgb8A1UnormSrgb:
            return GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        case PixelFormat::Etc2Rgba8Unorm:
            return GL_COMPRESSED_RGBA8_ETC2_EAC;
        case PixelFormat::Etc2Rgba8UnormSrgb:
            return GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC;
        case PixelFormat::EacR11Unorm:
            return GL_COMPRESSED_R11_EAC;
        case PixelFormat::EacR11Snorm:
            return GL_COMPRESSED_SIGNED_R11_EAC;
        case PixelFormat::EacRg11Unorm:
            return GL_COMPRESSED_RG11_EAC;
        case PixelFormat::EacRg11Snorm:
            return GL_COMPRESSED_SIGNED_RG11_EAC;
        default:
            fei::fatal("Unsupported PixelFormat");
    }
    return 0;
}

GLenum to_gl_texture_target(BitFlags<TextureUsage> usage, TextureType type) {
    if (usage.is_set(TextureUsage::Cubemap)) {
        return GL_TEXTURE_CUBE_MAP;
    } else if (type == TextureType::Texture1D) {
        return GL_TEXTURE_1D;
    } else if (type == TextureType::Texture2D) {
        return GL_TEXTURE_2D;
    } else if (type == TextureType::Texture3D) {
        return GL_TEXTURE_3D;
    }
    fei::fatal("Unsupported texture target");
    return 0;
}

GLenum to_gl_comparison_function(ComparisonKind func) {
    switch (func) {
        case ComparisonKind::Never:
            return GL_NEVER;
        case ComparisonKind::Less:
            return GL_LESS;
        case ComparisonKind::Equal:
            return GL_EQUAL;
        case ComparisonKind::LessEqual:
            return GL_LEQUAL;
        case ComparisonKind::Greater:
            return GL_GREATER;
        case ComparisonKind::NotEqual:
            return GL_NOTEQUAL;
        case ComparisonKind::GreaterEqual:
            return GL_GEQUAL;
        case ComparisonKind::Always:
            return GL_ALWAYS;
    }
    return 0;
}

GLenum to_gl_draw_elements_type(IndexFormat format) {
    switch (format) {
        case IndexFormat::Uint16:
            return GL_UNSIGNED_SHORT;
        case IndexFormat::Uint32:
            return GL_UNSIGNED_INT;
    }
    return 0;
}

GLenum to_gl_pixel_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8Unorm:
        case PixelFormat::R8Snorm:
        case PixelFormat::R8Uint:
        case PixelFormat::R8Sint:
        case PixelFormat::R16Uint:
        case PixelFormat::R16Sint:
        case PixelFormat::R16Unorm:
        case PixelFormat::R16Snorm:
        case PixelFormat::R16Float:
        case PixelFormat::R32Uint:
        case PixelFormat::R32Sint:
        case PixelFormat::R32Float:
            return GL_RED;
        case PixelFormat::Rg8Unorm:
        case PixelFormat::Rg8Snorm:
        case PixelFormat::Rg8Uint:
        case PixelFormat::Rg8Sint:
        case PixelFormat::Rg16Uint:
        case PixelFormat::Rg16Sint:
        case PixelFormat::Rg16Unorm:
        case PixelFormat::Rg16Snorm:
        case PixelFormat::Rg16Float:
        case PixelFormat::Rg32Uint:
        case PixelFormat::Rg32Sint:
        case PixelFormat::Rg32Float:
            return GL_RG;
        case PixelFormat::Rgba8Unorm:
        case PixelFormat::Rgba8UnormSrgb:
        case PixelFormat::Rgba8Snorm:
        case PixelFormat::Rgba8Uint:
        case PixelFormat::Rgba8Sint:
        case PixelFormat::Bgra8Unorm:
        case PixelFormat::Bgra8UnormSrgb:
        case PixelFormat::Rgb9e5Ufloat:
        case PixelFormat::Rgb10a2Uint:
        case PixelFormat::Rgb10a2Unorm:
        case PixelFormat::Rg11b10Ufloat:
        case PixelFormat::Rgba16Uint:
        case PixelFormat::Rgba16Sint:
        case PixelFormat::Rgba16Unorm:
        case PixelFormat::Rgba16Snorm:
        case PixelFormat::Rgba16Float:
        case PixelFormat::Rgba32Uint:
        case PixelFormat::Rgba32Sint:
        case PixelFormat::Rgba32Float:
            return GL_RGBA;
        case PixelFormat::Depth16Unorm:
        case PixelFormat::Depth24Plus:
        case PixelFormat::Depth32Float:
            return GL_DEPTH_COMPONENT;
        case PixelFormat::Depth24PlusStencil8:
        case PixelFormat::Depth32FloatStencil8:
            return GL_DEPTH_STENCIL;
        case PixelFormat::Stencil8:
            return GL_STENCIL_INDEX;
        default:
            fei::fatal("Unsupported PixelFormat");
    }
    return 0;
}

GLenum to_gl_pixel_type(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8Unorm:
        case PixelFormat::R8Uint:
        case PixelFormat::Rg8Unorm:
        case PixelFormat::Rg8Uint:
        case PixelFormat::Rgba8Unorm:
        case PixelFormat::Rgba8UnormSrgb:
        case PixelFormat::Rgba8Uint:
        case PixelFormat::Bgra8Unorm:
        case PixelFormat::Bgra8UnormSrgb:
            return GL_UNSIGNED_BYTE;
        case PixelFormat::R8Snorm:
        case PixelFormat::R8Sint:
        case PixelFormat::Rg8Snorm:
        case PixelFormat::Rg8Sint:
        case PixelFormat::Rgba8Snorm:
        case PixelFormat::Rgba8Sint:
            return GL_BYTE;
        case PixelFormat::R16Unorm:
        case PixelFormat::R16Uint:
        case PixelFormat::Rg16Unorm:
        case PixelFormat::Rg16Uint:
        case PixelFormat::Rgba16Unorm:
        case PixelFormat::Rgba16Uint:
        case PixelFormat::Depth16Unorm:
            return GL_UNSIGNED_SHORT;
        case PixelFormat::R16Snorm:
        case PixelFormat::R16Sint:
        case PixelFormat::Rg16Snorm:
        case PixelFormat::Rg16Sint:
        case PixelFormat::Rgba16Snorm:
        case PixelFormat::Rgba16Sint:
            return GL_SHORT;
        case PixelFormat::R32Uint:
        case PixelFormat::Rg32Uint:
        case PixelFormat::Rgba32Uint:
            return GL_UNSIGNED_INT;
        case PixelFormat::R32Sint:
        case PixelFormat::Rg32Sint:
        case PixelFormat::Rgba32Sint:
            return GL_INT;
        case PixelFormat::R16Float:
        case PixelFormat::Rg16Float:
        case PixelFormat::Rgba16Float:
            return GL_HALF_FLOAT;
        case PixelFormat::R32Float:
        case PixelFormat::Rg32Float:
        case PixelFormat::Rgba32Float:
        case PixelFormat::Depth32Float:
            return GL_FLOAT;
        default:
            fei::fatal("Unsupported PixelFormat");
    }
    return 0;
}

} // namespace fei
