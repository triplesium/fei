#pragma once
#include "graphics/graphics_device.hpp"
#include "graphics/shader_module.hpp"
#include "graphics_opengl/command_buffer_commands.hpp"
#include "graphics_opengl/deferred_resource.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fei {

struct OpenGLTextureReadbackState;

struct OpenGLPendingCommandSubmit {
    std::vector<opengl_commands::Command> commands;
};

struct OpenGLPendingBufferUpdate {
    std::shared_ptr<Buffer> buffer;
    std::uint32_t offset {0};
    std::vector<std::byte> data;
};

struct OpenGLPendingTextureUpdate {
    std::shared_ptr<Texture> texture;
    std::vector<std::byte> data;
    std::uint32_t x {0};
    std::uint32_t y {0};
    std::uint32_t z {0};
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::uint32_t depth {0};
    std::uint32_t mip_level {0};
    std::uint32_t layer {0};
};

struct OpenGLPendingTextureReadback {
    std::shared_ptr<OpenGLTextureReadbackState> state;
    std::shared_ptr<Texture> texture;
    std::size_t slot_index {0};
    std::uint32_t mip_level {0};
};

struct OpenGLFramebufferAttachmentCacheKey {
    const Texture* texture {nullptr};
    std::uint32_t mip_level {0};
    std::uint32_t layer {0};

    bool operator==(const OpenGLFramebufferAttachmentCacheKey&) const = default;
};

struct OpenGLFramebufferAttachmentCacheKeyHash {
    std::size_t
    operator()(const OpenGLFramebufferAttachmentCacheKey& key) const;
};

struct OpenGLFramebufferCacheKey {
    std::vector<OpenGLFramebufferAttachmentCacheKey> color_targets;
    bool has_depth_target {false};
    OpenGLFramebufferAttachmentCacheKey depth_target;

    bool operator==(const OpenGLFramebufferCacheKey&) const = default;
};

struct OpenGLFramebufferCacheKeyHash {
    std::size_t operator()(const OpenGLFramebufferCacheKey& key) const;
};

struct OpenGLResourceSetCacheKey {
    const ResourceLayout* layout {nullptr};
    std::vector<const BindableResource*> resources;

    bool operator==(const OpenGLResourceSetCacheKey&) const = default;
};

struct OpenGLResourceSetCacheKeyHash {
    std::size_t operator()(const OpenGLResourceSetCacheKey& key) const;
};

using OpenGLResourceCacheStats = GraphicsResourceCacheStats;

struct OpenGLFramebufferCacheEntry {
    std::shared_ptr<Framebuffer> framebuffer;
    std::uint64_t last_used_tick {0};
};

struct OpenGLResourceSetCacheEntry {
    std::shared_ptr<ResourceSet> resource_set;
    std::string name;
    std::uint64_t last_used_tick {0};
};

using OpenGLPendingOperation = std::variant<
    OpenGLPendingCommandSubmit,
    OpenGLPendingBufferUpdate,
    OpenGLPendingTextureUpdate,
    OpenGLPendingTextureReadback>;

class OpenGLDeviceState {
  public:
    void enqueue_operation(OpenGLPendingOperation operation);
    void enqueue_disposal(std::unique_ptr<DeferredResourceOpenGL> resource);
    void register_texture_readback(
        std::weak_ptr<OpenGLTextureReadbackState> readback
    );
    std::deque<OpenGLPendingOperation> take_pending_operations();
    std::vector<std::unique_ptr<DeferredResourceOpenGL>>
    take_pending_disposals();
    std::vector<std::shared_ptr<OpenGLTextureReadbackState>>
    live_texture_readbacks();
    std::shared_ptr<Framebuffer> get_or_create_framebuffer(
        const FramebufferDescription& desc,
        const std::function<std::shared_ptr<Framebuffer>()>& create
    );
    std::shared_ptr<ResourceSet> get_or_create_resource_set(
        const ResourceSetDescription& desc,
        const std::function<std::shared_ptr<ResourceSet>()>& create
    );
    OpenGLResourceCacheStats resource_cache_stats() const;
    void collect_resource_cache();
    void clear_resource_cache();

  private:
    friend class GraphicsDeviceOpenGL;

    static constexpr std::uint64_t ResourceCacheMaxIdleTicks = 120;

    mutable std::mutex m_mutex;
    std::deque<OpenGLPendingOperation> m_pending_operations;
    std::vector<std::unique_ptr<DeferredResourceOpenGL>> m_pending_disposals;
    std::vector<std::weak_ptr<OpenGLTextureReadbackState>> m_texture_readbacks;
    std::unordered_map<MappableResource*, std::byte*> m_mapped_resources;
    std::unordered_map<
        OpenGLFramebufferCacheKey,
        OpenGLFramebufferCacheEntry,
        OpenGLFramebufferCacheKeyHash>
        m_framebuffer_cache;
    std::unordered_map<
        OpenGLResourceSetCacheKey,
        OpenGLResourceSetCacheEntry,
        OpenGLResourceSetCacheKeyHash>
        m_resource_set_cache;
    OpenGLResourceCacheStats m_resource_cache_stats;
    std::unordered_map<std::string, GraphicsResourceSetSourceStats>
        m_resource_set_source_stats;
    std::uint64_t m_resource_cache_tick {0};
};

class GraphicsDeviceOpenGL : public GraphicsDevice {
  private:
    std::shared_ptr<OpenGLDeviceState> m_state;
    std::thread::id m_context_thread;

  public:
    GraphicsDeviceOpenGL();
    ~GraphicsDeviceOpenGL() override;

    GraphicsDeviceOpenGL(const GraphicsDeviceOpenGL&) = delete;
    GraphicsDeviceOpenGL& operator=(const GraphicsDeviceOpenGL&) = delete;
    GraphicsDeviceOpenGL(GraphicsDeviceOpenGL&&) noexcept = default;
    GraphicsDeviceOpenGL& operator=(GraphicsDeviceOpenGL&&) noexcept = default;

    std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) const override;
    std::shared_ptr<Buffer>
    create_buffer(const BufferDescription& desc) const override;
    std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) const override;
    std::shared_ptr<TextureView>
    create_texture_view(const TextureViewDescription& desc) const override;
    std::shared_ptr<CommandBuffer> create_command_buffer() const override;
    std::shared_ptr<Pipeline> create_render_pipeline(
        const RenderPipelineDescription& desc
    ) const override;
    std::shared_ptr<Pipeline> create_compute_pipeline(
        const ComputePipelineDescription& desc
    ) const override;
    std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) const override;
    std::shared_ptr<ResourceLayout> create_resource_layout(
        const ResourceLayoutDescription& desc
    ) const override;
    std::shared_ptr<ResourceSet>
    create_resource_set(const ResourceSetDescription& desc) const override;
    std::shared_ptr<Sampler>
    create_sampler(const SamplerDescription& desc) const override;
    void submit_commands(
        std::shared_ptr<CommandBuffer> command_buffer
    ) const override;

    void update_texture(
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
    ) const override;

    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) const override;

    MappedResource
    map(std::shared_ptr<MappableResource> resource,
        MapMode map_mode) const override;
    void unmap(std::shared_ptr<MappableResource> resource) const override;
    std::shared_ptr<TextureReadback>
    create_texture_readback(uint32 max_in_flight = 3) const override;

    void present(const Swapchain& swapchain) const override;
    void flush() const override;
    OpenGLResourceCacheStats resource_cache_stats() const override;

    std::shared_ptr<OpenGLDeviceState> state() { return m_state; }
    std::shared_ptr<const OpenGLDeviceState> state() const { return m_state; }

  private:
    void flush_pending_work() const;
    void execute_operation(const OpenGLPendingOperation& operation) const;
    void execute_update_buffer(const OpenGLPendingBufferUpdate& update) const;
    void execute_update_texture(const OpenGLPendingTextureUpdate& update) const;
    void execute_texture_readback(
        const OpenGLPendingTextureReadback& readback
    ) const;
    void collect_texture_readbacks() const;
    void flush_disposals() const;
    void assert_context_thread(const char* operation) const;
};

} // namespace fei
