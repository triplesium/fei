#include "graphics/opengl/graphics_device.hpp"

#include "base/log.hpp"
#include "graphics/enums.hpp"
#include "graphics/opengl/buffer.hpp"
#include "graphics/opengl/command_buffer.hpp"
#include "graphics/opengl/framebuffer.hpp"
#include "graphics/opengl/pipeline.hpp"
#include "graphics/opengl/resource.hpp"
#include "graphics/opengl/sampler.hpp"
#include "graphics/opengl/shader_module.hpp"
#include "graphics/opengl/texture.hpp"
#include "graphics/opengl/texture_view.hpp"
#include "graphics/opengl/utils.hpp"

#include <memory>

namespace fei {

GraphicsDeviceOpenGL::GraphicsDeviceOpenGL() {
    GLuint vao;
    glGenVertexArrays(1, &vao);
    opengl_check_error();
    glBindVertexArray(vao);
    opengl_check_error();

    // TODO: Abstract these states to be configurable
    glEnable(GL_CULL_FACE);
    opengl_check_error();
    glCullFace(GL_BACK);
    opengl_check_error();
    glEnable(GL_DEPTH_TEST);
    opengl_check_error();
}

std::shared_ptr<ShaderModule>
GraphicsDeviceOpenGL::create_shader_module(const ShaderDescription& desc) {
    return std::make_shared<ShaderOpenGL>(desc);
}

std::shared_ptr<Buffer>
GraphicsDeviceOpenGL::create_buffer(const BufferDescription& desc) {
    return std::make_shared<BufferOpenGL>(desc);
}

std::shared_ptr<Texture>
GraphicsDeviceOpenGL::create_texture(const TextureDescription& desc) {
    return std::make_shared<TextureOpenGL>(desc);
}

std::shared_ptr<TextureView>
GraphicsDeviceOpenGL::create_texture_view(const TextureViewDescription& desc) {
    return std::make_shared<TextureViewOpenGL>(desc);
}

std::shared_ptr<CommandBuffer> GraphicsDeviceOpenGL::create_command_buffer() {
    return std::make_shared<CommandBufferOpenGL>(*this);
}

std::shared_ptr<Pipeline> GraphicsDeviceOpenGL::create_render_pipeline(
    const RenderPipelineDescription& desc
) {
    return std::make_shared<PipelineOpenGL>(desc);
}

std::shared_ptr<Pipeline> GraphicsDeviceOpenGL::create_compute_pipeline(
    const ComputePipelineDescription& desc
) {
    return std::make_shared<PipelineOpenGL>(desc);
}

std::shared_ptr<Framebuffer>
GraphicsDeviceOpenGL::create_framebuffer(const FramebufferDescription& desc) {
    return std::make_shared<FramebufferOpenGL>(desc);
}

std::shared_ptr<ResourceLayout> GraphicsDeviceOpenGL::create_resource_layout(
    const ResourceLayoutDescription& desc
) {
    return std::make_shared<ResourceLayoutOpenGL>(desc);
}

std::shared_ptr<ResourceSet>
GraphicsDeviceOpenGL::create_resource_set(const ResourceSetDescription& desc) {
    return std::make_shared<ResourceSetOpenGL>(desc);
}

std::shared_ptr<Sampler>
GraphicsDeviceOpenGL::create_sampler(const SamplerDescription& desc) {
    return std::make_shared<SamplerOpenGL>(desc);
}

void GraphicsDeviceOpenGL::submit_commands(
    std::shared_ptr<CommandBuffer> command_buffer
) {}

void GraphicsDeviceOpenGL::update_texture(
    std::shared_ptr<Texture> texture,
    const void* data,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t depth,
    std::uint32_t mip_level,
    std::uint32_t layer
) {
    auto gl_texture = std::static_pointer_cast<TextureOpenGL>(texture);
    GLint texture_width, texture_height;
    glGetTextureLevelParameteriv(
        gl_texture->id(),
        mip_level,
        GL_TEXTURE_WIDTH,
        &texture_width
    );
    opengl_check_error();
    glGetTextureLevelParameteriv(
        gl_texture->id(),
        mip_level,
        GL_TEXTURE_HEIGHT,
        &texture_height
    );
    opengl_check_error();

    if (texture->usage().is_set(TextureUsage::Cubemap)) {
        glTextureSubImage3D(
            gl_texture->id(),
            mip_level,
            x,
            y,
            z,
            width,
            height,
            depth,
            gl_texture->gl_format(),
            gl_texture->gl_type(),
            data
        );
        opengl_check_error();
    } else {
        glTextureSubImage2D(
            gl_texture->id(),
            mip_level,
            x,
            y,
            width,
            height,
            gl_texture->gl_format(),
            gl_texture->gl_type(),
            data
        );
        opengl_check_error();
    }
}

void GraphicsDeviceOpenGL::update_buffer(
    std::shared_ptr<Buffer> buffer,
    std::uint32_t offset,
    const void* data,
    std::uint32_t size
) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    glNamedBufferData(
        buffer_gl->id(),
        size,
        data,
        to_gl_buffer_usage(buffer_gl->usages())
    );
    opengl_check_error();
}

MappedResource GraphicsDeviceOpenGL::map(
    std::shared_ptr<MappableResource> resource,
    MapMode map_mode
) {
    if (auto texture_gl = std::dynamic_pointer_cast<TextureOpenGL>(resource)) {
        std::uint32_t width = texture_gl->width();
        std::uint32_t height = texture_gl->height();
        std::uint32_t depth = texture_gl->depth();
        std::size_t bytes_per_pixel =
            get_pixel_format_size(texture_gl->format());

        std::size_t total_size = width * height * depth * bytes_per_pixel;

        auto* data = new std::byte[total_size];
        glGetTextureImage(
            texture_gl->id(),
            0, // mip level
            texture_gl->gl_format(),
            texture_gl->gl_type(),
            total_size,
            data
        );
        opengl_check_error();

        m_mapped_resources[resource.get()] = data;

        return MappedResource(
            resource,
            map_mode,
            std::span<std::byte>(data, total_size)
        );
    } else if (auto buffer_gl =
                   std::dynamic_pointer_cast<BufferOpenGL>(resource)) {
        void* ptr = glMapNamedBuffer(
            buffer_gl->id(),
            map_mode == MapMode::Read ? GL_READ_ONLY : GL_WRITE_ONLY
        );
        opengl_check_error();

        return MappedResource(
            resource,
            map_mode,
            std::span<std::byte>(
                reinterpret_cast<std::byte*>(ptr),
                buffer_gl->size()
            )
        );
    }
    fei::fatal("Unknown MappableResource type in GraphicsDeviceOpenGL::map");
    return MappedResource(nullptr, MapMode::Read, std::span<std::byte>());
}

void GraphicsDeviceOpenGL::unmap(std::shared_ptr<MappableResource> resource) {
    if (auto texture_gl = std::dynamic_pointer_cast<TextureOpenGL>(resource)) {
        auto it = m_mapped_resources.find(resource.get());
        if (it != m_mapped_resources.end()) {
            delete[] it->second;
            m_mapped_resources.erase(it);
        }
        return;
    } else if (auto buffer_gl =
                   std::dynamic_pointer_cast<BufferOpenGL>(resource)) {
        glUnmapNamedBuffer(buffer_gl->id());
        opengl_check_error();
        return;
    }
    fei::fatal("Unknown MappableResource type in GraphicsDeviceOpenGL::unmap");
}

std::shared_ptr<Framebuffer> GraphicsDeviceOpenGL::main_framebuffer() {
    // Return the default framebuffer (ID 0)
    return std::shared_ptr<FramebufferOpenGL>(new FramebufferOpenGL(0));
}

} // namespace fei
