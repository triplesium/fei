#include "graphics_opengl/graphics_device.hpp"

#include "base/log.hpp"
#include "graphics/enums.hpp"
#include "graphics/utils.hpp"
#include "graphics_opengl/buffer.hpp"
#include "graphics_opengl/command_buffer.hpp"
#include "graphics_opengl/command_buffer_executor.hpp"
#include "graphics_opengl/framebuffer.hpp"
#include "graphics_opengl/pipeline.hpp"
#include "graphics_opengl/resource.hpp"
#include "graphics_opengl/sampler.hpp"
#include "graphics_opengl/shader_module.hpp"
#include "graphics_opengl/texture.hpp"
#include "graphics_opengl/texture_readback.hpp"
#include "graphics_opengl/texture_view.hpp"
#include "graphics_opengl/utils.hpp"
#include "profiling/profiling.hpp"

#include <cstring>
#include <deque>
#include <memory>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

namespace {

template<class>
inline constexpr bool always_false_v = false;

std::vector<std::byte> copy_bytes(const void* data, std::size_t size) {
    std::vector<std::byte> bytes(size);
    if (size == 0) {
        return bytes;
    }
    if (data == nullptr) {
        fatal("GraphicsDeviceOpenGL received null data for {} byte copy", size);
    }
    std::memcpy(bytes.data(), data, size);
    return bytes;
}

template<class ResourceT, class... Args>
std::shared_ptr<ResourceT> make_deferred_resource(
    const std::shared_ptr<OpenGLDeviceState>& state,
    Args&&... args
) {
    static_assert(std::is_base_of_v<DeferredResourceOpenGL, ResourceT>);
    return std::shared_ptr<ResourceT>(
        new ResourceT(std::forward<Args>(args)...),
        [state](ResourceT* resource) {
            state->enqueue_disposal(
                std::unique_ptr<DeferredResourceOpenGL>(resource)
            );
        }
    );
}

} // namespace

void OpenGLDeviceState::enqueue_operation(OpenGLPendingOperation operation) {
    std::scoped_lock lock(m_mutex);
    m_pending_operations.push_back(std::move(operation));
}

void OpenGLDeviceState::enqueue_disposal(
    std::unique_ptr<DeferredResourceOpenGL> resource
) {
    if (!resource) {
        return;
    }

    std::scoped_lock lock(m_mutex);
    m_pending_disposals.push_back(std::move(resource));
}

void OpenGLDeviceState::register_texture_readback(
    std::weak_ptr<OpenGLTextureReadbackState> readback
) {
    std::scoped_lock lock(m_mutex);
    m_texture_readbacks.push_back(std::move(readback));
}

std::deque<OpenGLPendingOperation>
OpenGLDeviceState::take_pending_operations() {
    std::deque<OpenGLPendingOperation> operations;
    std::scoped_lock lock(m_mutex);
    operations.swap(m_pending_operations);
    return operations;
}

std::vector<std::unique_ptr<DeferredResourceOpenGL>>
OpenGLDeviceState::take_pending_disposals() {
    std::vector<std::unique_ptr<DeferredResourceOpenGL>> disposals;
    std::scoped_lock lock(m_mutex);
    disposals.swap(m_pending_disposals);
    return disposals;
}

std::vector<std::shared_ptr<OpenGLTextureReadbackState>>
OpenGLDeviceState::live_texture_readbacks() {
    std::vector<std::shared_ptr<OpenGLTextureReadbackState>> readbacks;
    std::scoped_lock lock(m_mutex);

    std::vector<std::weak_ptr<OpenGLTextureReadbackState>> live;
    live.reserve(m_texture_readbacks.size());
    for (auto& readback : m_texture_readbacks) {
        if (auto locked = readback.lock()) {
            readbacks.push_back(locked);
            live.push_back(locked);
        }
    }
    m_texture_readbacks = std::move(live);

    return readbacks;
}

GraphicsDeviceOpenGL::GraphicsDeviceOpenGL() :
    m_state(std::make_shared<OpenGLDeviceState>()),
    m_context_thread(std::this_thread::get_id()) {
    GLuint vao;
    FEI_GL_CALL(glGenVertexArrays(1, &vao));
    FEI_GL_CALL(glBindVertexArray(vao));

    // TODO: Abstract these states to be configurable
    FEI_GL_CALL(glEnable(GL_CULL_FACE));
    FEI_GL_CALL(glCullFace(GL_BACK));
    FEI_GL_CALL(glEnable(GL_DEPTH_TEST));
    FEI_GL_CALL(glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS));
}

GraphicsDeviceOpenGL::~GraphicsDeviceOpenGL() {
    if (!m_state) {
        return;
    }
    flush_pending_work();
}

std::shared_ptr<ShaderModule> GraphicsDeviceOpenGL::create_shader_module(
    const ShaderDescription& desc
) const {
    return make_deferred_resource<ShaderOpenGL>(m_state, desc);
}

std::shared_ptr<Buffer>
GraphicsDeviceOpenGL::create_buffer(const BufferDescription& desc) const {
    return make_deferred_resource<BufferOpenGL>(m_state, desc);
}

std::shared_ptr<Texture>
GraphicsDeviceOpenGL::create_texture(const TextureDescription& desc) const {
    return make_deferred_resource<TextureOpenGL>(m_state, desc);
}

std::shared_ptr<TextureView> GraphicsDeviceOpenGL::create_texture_view(
    const TextureViewDescription& desc
) const {
    return make_deferred_resource<TextureViewOpenGL>(m_state, desc);
}

std::shared_ptr<CommandBuffer>
GraphicsDeviceOpenGL::create_command_buffer() const {
    return std::make_shared<CommandBufferOpenGL>(*this);
}

std::shared_ptr<Pipeline> GraphicsDeviceOpenGL::create_render_pipeline(
    const RenderPipelineDescription& desc
) const {
    return make_deferred_resource<PipelineOpenGL>(m_state, desc);
}

std::shared_ptr<Pipeline> GraphicsDeviceOpenGL::create_compute_pipeline(
    const ComputePipelineDescription& desc
) const {
    return make_deferred_resource<PipelineOpenGL>(m_state, desc);
}

std::shared_ptr<Framebuffer> GraphicsDeviceOpenGL::create_framebuffer(
    const FramebufferDescription& desc
) const {
    return make_deferred_resource<FramebufferOpenGL>(m_state, desc);
}

std::shared_ptr<ResourceLayout> GraphicsDeviceOpenGL::create_resource_layout(
    const ResourceLayoutDescription& desc
) const {
    return std::make_shared<ResourceLayoutOpenGL>(desc);
}

std::shared_ptr<ResourceSet> GraphicsDeviceOpenGL::create_resource_set(
    const ResourceSetDescription& desc
) const {
    return std::make_shared<ResourceSetOpenGL>(desc);
}

std::shared_ptr<Sampler>
GraphicsDeviceOpenGL::create_sampler(const SamplerDescription& desc) const {
    return make_deferred_resource<SamplerOpenGL>(m_state, desc);
}

void GraphicsDeviceOpenGL::submit_commands(
    std::shared_ptr<CommandBuffer> command_buffer
) const {
    FEI_PROFILE_SCOPE("OpenGL Submit Commands");
    auto command_buffer_gl =
        std::dynamic_pointer_cast<CommandBufferOpenGL>(command_buffer);
    if (!command_buffer_gl) {
        fatal(
            "GraphicsDeviceOpenGL::submit_commands received a non-OpenGL "
            "command buffer"
        );
    }

    command_buffer_gl->ensure_executable("submit_commands");
    auto commands = command_buffer_gl->m_commands;
    command_buffer_gl->mark_submitted();

    m_state->enqueue_operation(
        OpenGLPendingCommandSubmit {.commands = std::move(commands)}
    );
}

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
) const {
    FEI_PROFILE_SCOPE("OpenGL Queue Texture Update");
    const auto byte_count = static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height) *
                            static_cast<std::size_t>(depth) *
                            get_pixel_format_size(texture->format());
    auto bytes = copy_bytes(data, byte_count);

    m_state->enqueue_operation(
        OpenGLPendingTextureUpdate {
            .texture = std::move(texture),
            .data = std::move(bytes),
            .x = x,
            .y = y,
            .z = z,
            .width = width,
            .height = height,
            .depth = depth,
            .mip_level = mip_level,
            .layer = layer,
        }
    );
}

void GraphicsDeviceOpenGL::update_buffer(
    std::shared_ptr<Buffer> buffer,
    std::uint32_t offset,
    const void* data,
    std::uint32_t size
) const {
    FEI_PROFILE_SCOPE("OpenGL Queue Buffer Update");
    auto bytes = copy_bytes(data, size);

    m_state->enqueue_operation(
        OpenGLPendingBufferUpdate {
            .buffer = std::move(buffer),
            .offset = offset,
            .data = std::move(bytes),
        }
    );
}

MappedResource GraphicsDeviceOpenGL::map(
    std::shared_ptr<MappableResource> resource,
    MapMode map_mode
) const {
    FEI_PROFILE_SCOPE("OpenGL Map Resource");
    assert_context_thread("GraphicsDeviceOpenGL::map");
    flush();

    if (auto texture_gl = std::dynamic_pointer_cast<TextureOpenGL>(resource)) {
        if (!texture_gl->usage().is_set(TextureUsage::Staging)) {
            fatal(
                "GraphicsDeviceOpenGL::map can only map staging textures. "
                "Use TextureReadback for GPU texture readback."
            );
        }
        if (map_mode != MapMode::Read) {
            fatal("GraphicsDeviceOpenGL::map supports read-only texture maps");
        }

        texture_gl->ensure_created();
        std::uint32_t width = texture_gl->width();
        std::uint32_t height = texture_gl->height();
        std::uint32_t depth = texture_gl->depth();
        std::size_t bytes_per_pixel =
            get_pixel_format_size(texture_gl->format());

        std::size_t total_size =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
            static_cast<std::size_t>(depth) * bytes_per_pixel;

        auto* data = new std::byte[total_size];
        FEI_GL_CALL(glGetTextureImage(
            texture_gl->id(),
            0, // mip level
            texture_gl->gl_format(),
            texture_gl->gl_type(),
            static_cast<GLsizei>(total_size),
            data
        ));

        {
            std::scoped_lock lock(m_state->m_mutex);
            m_state->m_mapped_resources[resource.get()] = data;
        }

        return MappedResource(
            resource,
            map_mode,
            std::span<std::byte>(data, total_size)
        );
    } else if (
        auto buffer_gl = std::dynamic_pointer_cast<BufferOpenGL>(resource)
    ) {
        buffer_gl->ensure_created();
        void* ptr = FEI_GL_CALL(glMapNamedBuffer(
            buffer_gl->id(),
            map_mode == MapMode::Read ? GL_READ_ONLY : GL_WRITE_ONLY
        ));

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

void GraphicsDeviceOpenGL::unmap(
    std::shared_ptr<MappableResource> resource
) const {
    FEI_PROFILE_SCOPE("OpenGL Unmap Resource");
    assert_context_thread("GraphicsDeviceOpenGL::unmap");
    flush();

    if (std::dynamic_pointer_cast<TextureOpenGL>(resource)) {
        std::scoped_lock lock(m_state->m_mutex);
        auto it = m_state->m_mapped_resources.find(resource.get());
        if (it != m_state->m_mapped_resources.end()) {
            delete[] it->second;
            m_state->m_mapped_resources.erase(it);
        }
        return;
    } else if (
        auto buffer_gl = std::dynamic_pointer_cast<BufferOpenGL>(resource)
    ) {
        buffer_gl->ensure_created();
        FEI_GL_CALL(glUnmapNamedBuffer(buffer_gl->id()));
        return;
    }
    fei::fatal("Unknown MappableResource type in GraphicsDeviceOpenGL::unmap");
}

std::shared_ptr<TextureReadback>
GraphicsDeviceOpenGL::create_texture_readback(uint32 max_in_flight) const {
    return std::make_shared<TextureReadbackOpenGL>(m_state, max_in_flight);
}

std::shared_ptr<const Framebuffer>
GraphicsDeviceOpenGL::main_framebuffer() const {
    // Return the default framebuffer (ID 0)
    return std::shared_ptr<FramebufferOpenGL>(new FramebufferOpenGL(0));
}

void GraphicsDeviceOpenGL::flush() const {
    FEI_PROFILE_SCOPE("OpenGL Device Flush");
    assert_context_thread("GraphicsDeviceOpenGL::flush");
    flush_pending_work();
}

void GraphicsDeviceOpenGL::flush_pending_work() const {
    FEI_PROFILE_SCOPE("OpenGL Flush Pending Work");
    assert_context_thread("GraphicsDeviceOpenGL::flush_pending_work");
    auto operations = m_state->take_pending_operations();

    for (const auto& operation : operations) {
        execute_operation(operation);
    }

    collect_texture_readbacks();
    flush_disposals();
}

void GraphicsDeviceOpenGL::execute_operation(
    const OpenGLPendingOperation& operation
) const {
    std::visit(
        [this](const auto& op) {
            using OperationT = std::decay_t<decltype(op)>;
            if constexpr (
                std::is_same_v<OperationT, OpenGLPendingCommandSubmit>
            ) {
                CommandBufferExecutorOpenGL executor(*this);
                executor.execute(op.commands);
            } else if constexpr (
                std::is_same_v<OperationT, OpenGLPendingBufferUpdate>
            ) {
                execute_update_buffer(op);
            } else if constexpr (
                std::is_same_v<OperationT, OpenGLPendingTextureUpdate>
            ) {
                execute_update_texture(op);
            } else if constexpr (
                std::is_same_v<OperationT, OpenGLPendingTextureReadback>
            ) {
                execute_texture_readback(op);
            } else {
                static_assert(always_false_v<OperationT>);
            }
        },
        operation
    );
}

void GraphicsDeviceOpenGL::execute_update_buffer(
    const OpenGLPendingBufferUpdate& update
) const {
    FEI_PROFILE_SCOPE("OpenGL Buffer Upload");
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(update.buffer);
    buffer_gl->ensure_created();

    if (update.offset == 0 && update.data.size() == buffer_gl->size()) {
        FEI_GL_CALL(glNamedBufferData(
            buffer_gl->id(),
            to_gl_sizeiptr(update.data.size()),
            update.data.data(),
            to_gl_buffer_usage(buffer_gl->usages())
        ));
        return;
    }

    FEI_GL_CALL(glNamedBufferSubData(
        buffer_gl->id(),
        static_cast<GLintptr>(update.offset),
        to_gl_sizeiptr(update.data.size()),
        update.data.data()
    ));
}

void GraphicsDeviceOpenGL::execute_update_texture(
    const OpenGLPendingTextureUpdate& update
) const {
    FEI_PROFILE_SCOPE("OpenGL Texture Upload");
    auto gl_texture = std::static_pointer_cast<TextureOpenGL>(update.texture);
    gl_texture->ensure_created();

    if (update.texture->usage().is_set(TextureUsage::Cubemap)) {
        FEI_GL_CALL(glTextureSubImage3D(
            gl_texture->id(),
            static_cast<GLint>(update.mip_level),
            static_cast<GLint>(update.x),
            static_cast<GLint>(update.y),
            static_cast<GLint>(update.z),
            static_cast<GLsizei>(update.width),
            static_cast<GLsizei>(update.height),
            static_cast<GLsizei>(update.depth),
            gl_texture->gl_format(),
            gl_texture->gl_type(),
            update.data.data()
        ));
    } else {
        FEI_GL_CALL(glTextureSubImage2D(
            gl_texture->id(),
            static_cast<GLint>(update.mip_level),
            static_cast<GLint>(update.x),
            static_cast<GLint>(update.y),
            static_cast<GLsizei>(update.width),
            static_cast<GLsizei>(update.height),
            gl_texture->gl_format(),
            gl_texture->gl_type(),
            update.data.data()
        ));
    }
}

void GraphicsDeviceOpenGL::execute_texture_readback(
    const OpenGLPendingTextureReadback& readback
) const {
    fei::execute_texture_readback(readback);
}

void GraphicsDeviceOpenGL::collect_texture_readbacks() const {
    for (const auto& readback : m_state->live_texture_readbacks()) {
        fei::collect_ready_texture_readbacks(readback);
    }
}

void GraphicsDeviceOpenGL::flush_disposals() const {
    while (true) {
        auto disposals = m_state->take_pending_disposals();
        if (disposals.empty()) {
            return;
        }

        for (auto& resource : disposals) {
            resource->dispose();
        }
    }
}

void GraphicsDeviceOpenGL::assert_context_thread(const char* operation) const {
    if (std::this_thread::get_id() == m_context_thread) {
        return;
    }

    fatal("{} must run on the OpenGL context thread", operation);
}

} // namespace fei
