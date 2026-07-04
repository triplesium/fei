#pragma once
#include "base/types.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace fei {

namespace detail {

template<typename Tag>
struct [[nodiscard]] RgHandle {
    static constexpr uint32 InvalidIndex = static_cast<uint32>(-1);

    uint32 index {InvalidIndex};
    uint32 generation {0};

    [[nodiscard]] bool is_valid() const { return index != InvalidIndex; }

    bool operator==(const RgHandle&) const = default;
};

struct RgTextureTag;
struct RgResourceSetTag;

} // namespace detail

using RgTextureHandle = detail::RgHandle<detail::RgTextureTag>;
using RgResourceSetHandle = detail::RgHandle<detail::RgResourceSetTag>;

class RenderGraphResourceBinding {
  public:
    using Resource =
        std::variant<RgTextureHandle, std::shared_ptr<const BindableResource>>;

    RenderGraphResourceBinding(RgTextureHandle texture) : m_resource(texture) {}

    template<typename ResourceT>
        requires std::
            derived_from<std::remove_cv_t<ResourceT>, BindableResource>
        RenderGraphResourceBinding(std::shared_ptr<ResourceT> resource) :
        m_resource(
            std::static_pointer_cast<const BindableResource>(
                std::move(resource)
            )
        ) {}

    [[nodiscard]] bool is_texture() const {
        return std::holds_alternative<RgTextureHandle>(m_resource);
    }

    [[nodiscard]] RgTextureHandle texture() const {
        return std::get<RgTextureHandle>(m_resource);
    }

    [[nodiscard]] const std::shared_ptr<const BindableResource>&
    resource() const {
        return std::get<std::shared_ptr<const BindableResource>>(m_resource);
    }

  private:
    Resource m_resource;
};

enum class RenderGraphAccess : uint8 {
    TextureRead,
    ColorAttachmentWrite,
    DepthStencilWrite,
    TextureReadWrite,
    BlitSource,
};

struct RenderGraphStats {
    uint64 total_passes {0};
    uint64 active_passes {0};
    uint64 culled_passes {0};
    uint64 transient_texture_requests {0};
    uint64 transient_texture_hits {0};
    uint64 transient_texture_creates {0};
    std::size_t texture_pool_size {0};
};

struct RgTextureUseDebugInfo {
    RgTextureHandle handle;
    std::string texture_name;
    RenderGraphAccess access {RenderGraphAccess::TextureRead};
    std::string access_name;
};

struct RgPassDebugInfo {
    uint32 index {0};
    std::string name;
    bool active {false};
    bool side_effect {false};
    std::vector<uint32> dependencies;
    std::vector<RgTextureUseDebugInfo> reads;
    std::vector<RgTextureUseDebugInfo> writes;
};

struct RgTextureDebugInfo {
    uint32 index {0};
    std::string name;
    bool active {false};
    bool imported {false};
    uint32 width {0};
    uint32 height {0};
    uint32 depth {0};
    uint32 mip_level {1};
    uint32 layer {1};
    std::string format;
    std::string usage;
    std::string type;
    uint32 version_count {0};
    uint32 first_active_use {RgTextureHandle::InvalidIndex};
    uint32 last_active_use {RgTextureHandle::InvalidIndex};
};

struct RgResourceSetBindingDebugInfo {
    uint32 index {0};
    std::string kind;
    std::string resource_name;
    bool valid {false};
    RgTextureHandle texture;
};

struct RgResourceSetDebugInfo {
    uint32 index {0};
    uint32 generation {0};
    uint32 pass_index {0};
    std::string name;
    bool active {false};
    bool resolved {false};
    bool has_layout {false};
    std::vector<RgResourceSetBindingDebugInfo> bindings;
};

struct RgDebugInfo {
    bool compiled {false};
    std::string compile_error;
    std::vector<uint32> active_order;
    std::vector<std::string> active_pass_names;
    std::vector<RgPassDebugInfo> passes;
    std::vector<RgTextureDebugInfo> textures;
    std::vector<RgResourceSetDebugInfo> resource_sets;
    RenderGraphStats stats;
};

class RenderGraphBlackboard {
  private:
    struct EntryBase {
        virtual ~EntryBase() = default;
    };

    template<typename T>
    struct Entry : EntryBase {
        template<typename... Args>
        explicit Entry(Args&&... args) : value(std::forward<Args>(args)...) {}

        T value;
    };

    std::unordered_map<TypeId, std::unique_ptr<EntryBase>> m_entries;

  public:
    void clear() { m_entries.clear(); }

    RenderGraphBlackboard() = default;
    RenderGraphBlackboard(const RenderGraphBlackboard&) = delete;
    RenderGraphBlackboard& operator=(const RenderGraphBlackboard&) = delete;
    RenderGraphBlackboard(RenderGraphBlackboard&&) noexcept = default;
    RenderGraphBlackboard&
    operator=(RenderGraphBlackboard&&) noexcept = default;

    template<typename T, typename... Args>
    [[nodiscard]]
    T& emplace(Args&&... args) {
        auto entry = std::make_unique<Entry<T>>(std::forward<Args>(args)...);
        auto* value = &entry->value;
        m_entries[type_id<T>()] = std::move(entry);
        return *value;
    }

    template<typename T>
    [[nodiscard]] bool contains() const {
        return m_entries.contains(type_id<T>());
    }

    template<typename T>
    [[nodiscard]]
    T& get() {
        return static_cast<Entry<T>&>(*m_entries.at(type_id<T>())).value;
    }

    template<typename T>
    [[nodiscard]]
    const T& get() const {
        return static_cast<const Entry<T>&>(*m_entries.at(type_id<T>())).value;
    }
};

class RenderGraph;

class RenderGraphBuilder {
  private:
    RenderGraph& m_graph;
    uint32 m_pass_index;

    RenderGraphBuilder(RenderGraph& graph, uint32 pass_index) :
        m_graph(graph), m_pass_index(pass_index) {}

    friend class RenderGraph;

  public:
    [[nodiscard]] RgTextureHandle
    create_texture(std::string name, TextureDescription desc);

    [[nodiscard]] RgTextureHandle
    import_texture(std::string name, std::shared_ptr<Texture> texture);

    void read_texture(RgTextureHandle handle, RenderGraphAccess access);

    [[nodiscard]] RgTextureHandle
    write_texture(RgTextureHandle handle, RenderGraphAccess access);

    [[nodiscard]] RgResourceSetHandle create_resource_set(
        std::string name,
        std::shared_ptr<const ResourceLayout> layout,
        std::vector<RenderGraphResourceBinding> bindings
    );

    void side_effect();
};

class RenderGraphContext {
  private:
    RenderGraph& m_graph;
    const GraphicsDevice& m_device;
    CommandBuffer& m_command_buffer;

    RenderGraphContext(
        RenderGraph& graph,
        const GraphicsDevice& device,
        CommandBuffer& command_buffer
    ) : m_graph(graph), m_device(device), m_command_buffer(command_buffer) {}

    friend class RenderGraph;

  public:
    const GraphicsDevice& device() const { return m_device; }
    CommandBuffer& command_buffer() { return m_command_buffer; }

    [[nodiscard]] std::shared_ptr<Texture>
    texture(RgTextureHandle handle) const;

    [[nodiscard]] std::shared_ptr<ResourceSet>
    resource_set(RgResourceSetHandle handle) const;
};

class RenderGraph {
  public:
    struct Empty {};

  private:
    struct TextureVersion {
        int writer_pass {-1};
        RenderGraphAccess write_access {RenderGraphAccess::TextureRead};
    };

    struct TextureResource {
        std::string name;
        TextureDescription description;
        std::shared_ptr<Texture> texture;
        bool imported {false};
        uint32 first_active_use {RgTextureHandle::InvalidIndex};
        uint32 last_active_use {RgTextureHandle::InvalidIndex};
        std::vector<TextureVersion> versions;
    };

    struct TexturePoolKey {
        uint32 width {0};
        uint32 height {0};
        uint32 depth {0};
        uint32 mip_level {1};
        uint32 layer {1};
        PixelFormat texture_format {};
        std::underlying_type_t<TextureUsage> texture_usage {0};
        TextureType texture_type {};

        bool operator==(const TexturePoolKey&) const = default;
    };

    struct TexturePoolKeyHash {
        std::size_t operator()(const TexturePoolKey& key) const {
            std::size_t seed = 0;
            auto combine = [&seed](std::size_t value) {
                seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };

            combine(key.width);
            combine(key.height);
            combine(key.depth);
            combine(key.mip_level);
            combine(key.layer);
            combine(static_cast<std::size_t>(key.texture_format));
            combine(static_cast<std::size_t>(key.texture_usage));
            combine(static_cast<std::size_t>(key.texture_type));
            return seed;
        }
    };

    struct PooledTexture {
        std::shared_ptr<Texture> texture;
        uint64 last_used_frame {0};
    };

    struct TextureUse {
        RgTextureHandle handle;
        RenderGraphAccess access;
    };

    struct ResourceSetResource {
        std::string name;
        std::shared_ptr<const ResourceLayout> layout;
        std::vector<RenderGraphResourceBinding> bindings;
        std::shared_ptr<ResourceSet> resource_set;
        uint32 pass_index {0};
        uint32 generation {0};
    };

    struct ResourceSetCacheKey {
        const ResourceLayout* layout {nullptr};
        std::vector<const BindableResource*> resources;

        bool operator==(const ResourceSetCacheKey&) const = default;
    };

    struct ResourceSetCacheKeyHash {
        std::size_t operator()(const ResourceSetCacheKey& key) const;
    };

    struct CachedResourceSet {
        std::shared_ptr<ResourceSet> resource_set;
        std::shared_ptr<const ResourceLayout> layout;
        std::vector<std::shared_ptr<const BindableResource>> resources;
        uint64 last_used_frame {0};
    };

    struct PassBase {
        explicit PassBase(std::string pass_name) : name(std::move(pass_name)) {}
        virtual ~PassBase() = default;

        std::string name;
        std::vector<TextureUse> reads;
        std::vector<TextureUse> writes;
        bool side_effect {false};
        bool active {false};

        virtual void execute(RenderGraphContext& context) = 0;
    };

    template<typename Data, typename Execute>
    struct Pass final : PassBase {
        Pass(std::string name, Execute&& execute) :
            PassBase(std::move(name)),
            execute_fn(std::forward<Execute>(execute)) {}

        Data data {};
        Execute execute_fn;

        void execute(RenderGraphContext& context) override {
            if constexpr (
                std::invocable<Execute, RenderGraphContext&, const Data&>
            ) {
                execute_fn(context, data);
            } else {
                execute_fn(context);
            }
        }
    };

    std::vector<TextureResource> m_textures;
    std::vector<ResourceSetResource> m_resource_sets;
    std::vector<std::unique_ptr<PassBase>> m_passes;
    std::vector<uint32> m_compiled_order;
    std::unordered_map<
        TexturePoolKey,
        std::vector<PooledTexture>,
        TexturePoolKeyHash>
        m_texture_pool;
    std::unordered_map<
        ResourceSetCacheKey,
        CachedResourceSet,
        ResourceSetCacheKeyHash>
        m_resource_set_cache;
    RenderGraphBlackboard m_blackboard;
    std::string m_compile_error;
    RenderGraphStats m_stats;
    RgDebugInfo m_debug_info;
    uint64 m_frame_index {0};
    bool m_compiled {false};

    static constexpr uint64 TexturePoolMaxIdleFrames = 120;

    [[nodiscard]] static TexturePoolKey
    texture_pool_key(const TextureDescription& desc);
    [[nodiscard]] bool is_valid(RgTextureHandle handle) const;
    [[nodiscard]] bool is_valid(RgResourceSetHandle handle) const;
    [[nodiscard]] TextureResource& texture_resource(RgTextureHandle handle);
    [[nodiscard]] const TextureResource&
    texture_resource(RgTextureHandle handle) const;
    [[nodiscard]] ResourceSetResource&
    resource_set_resource(RgResourceSetHandle handle);
    [[nodiscard]] const ResourceSetResource&
    resource_set_resource(RgResourceSetHandle handle) const;
    [[nodiscard]] std::shared_ptr<Texture> acquire_texture(
        const GraphicsDevice& device,
        const TextureDescription& desc
    );
    void release_texture(TextureResource& resource);
    [[nodiscard]] std::size_t texture_pool_size() const;
    void update_texture_pool_stats();
    void collect_texture_pool();
    void collect_resource_set_cache();
    void update_debug_info(const std::vector<std::vector<uint32>>& incoming);
    void sync_debug_stats();
    void ensure_texture_allocated(
        RgTextureHandle handle,
        const GraphicsDevice& device
    );
    [[nodiscard]] std::shared_ptr<ResourceSet> resolve_resource_set(
        RgResourceSetHandle handle,
        const GraphicsDevice& device
    );
    void release_textures_after_pass(uint32 use_index);

    [[nodiscard]] RgTextureHandle
    create_texture(std::string name, TextureDescription desc);
    [[nodiscard]] RgTextureHandle
    import_texture(std::string name, std::shared_ptr<Texture> texture);
    void read_texture(
        uint32 pass_index,
        RgTextureHandle handle,
        RenderGraphAccess access
    );
    [[nodiscard]] RgTextureHandle write_texture(
        uint32 pass_index,
        RgTextureHandle handle,
        RenderGraphAccess access
    );
    [[nodiscard]] RgResourceSetHandle create_resource_set(
        uint32 pass_index,
        std::string name,
        std::shared_ptr<const ResourceLayout> layout,
        std::vector<RenderGraphResourceBinding> bindings
    );
    void side_effect(uint32 pass_index);

    friend class RenderGraphBuilder;
    friend class RenderGraphContext;

  public:
    RenderGraph() = default;
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) noexcept = default;
    RenderGraph& operator=(RenderGraph&&) noexcept = default;

    void clear();

    RenderGraphBlackboard& blackboard() { return m_blackboard; }
    const RenderGraphBlackboard& blackboard() const { return m_blackboard; }

    template<typename Data, typename Setup, typename Execute>
    Data& add_pass(std::string name, Setup&& setup, Execute&& execute) {
        const auto pass_index = static_cast<uint32>(m_passes.size());
        auto pass = std::make_unique<Pass<Data, std::decay_t<Execute>>>(
            std::move(name),
            std::forward<Execute>(execute)
        );
        auto* pass_ptr = pass.get();
        m_passes.push_back(std::move(pass));

        RenderGraphBuilder builder(*this, pass_index);
        std::forward<Setup>(setup)(builder, pass_ptr->data);
        m_compiled = false;
        return pass_ptr->data;
    }

    bool compile();
    void execute(const GraphicsDevice& device, CommandBuffer& command_buffer);

    [[nodiscard]] std::string_view compile_error() const {
        return m_compile_error;
    }

    [[nodiscard]] const std::vector<uint32>& compiled_order() const {
        return m_compiled_order;
    }

    [[nodiscard]] const RenderGraphStats& stats() const { return m_stats; }
    [[nodiscard]] const RgDebugInfo& debug_info() const { return m_debug_info; }
};

} // namespace fei
