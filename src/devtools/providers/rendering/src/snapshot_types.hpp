#pragma once

#include "graphics/graphics_device.hpp"
#include "refl/reflect.hpp"
#include "rendering/render_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fei::devtools::rendering {

struct FEI_REFLECT TextureUseSnapshot {
    std::uint32_t index {0};
    std::uint32_t generation {0};
    std::string name;
    std::string access;
};

struct FEI_REFLECT RenderPassSnapshot {
    std::uint32_t index {0};
    std::string name;
    bool active {false};
    bool side_effect {false};
    std::vector<std::uint32_t> dependencies;
    std::vector<TextureUseSnapshot> reads;
    std::vector<TextureUseSnapshot> writes;
};

struct FEI_REFLECT TextureSnapshot {
    std::uint32_t index {0};
    std::string name;
    bool active {false};
    bool imported {false};
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::uint32_t depth {0};
    std::uint32_t mip_level {1};
    std::uint32_t layer {1};
    std::string format;
    std::string usage;
    std::string type;
    std::uint32_t sample_count {1};
    std::uint32_t version_count {0};
    std::uint32_t first_active_use {RgTextureHandle::InvalidIndex};
    std::uint32_t last_active_use {RgTextureHandle::InvalidIndex};
};

struct FEI_REFLECT ResourceBindingSnapshot {
    std::uint32_t index {0};
    std::string kind;
    std::string resource_name;
    bool valid {false};
    bool has_texture {false};
    std::uint32_t texture_index {RgTextureHandle::InvalidIndex};
    std::uint32_t texture_generation {0};
};

struct FEI_REFLECT ResourceSetSnapshot {
    std::uint32_t index {0};
    std::uint32_t generation {0};
    std::uint32_t pass_index {0};
    std::string name;
    bool active {false};
    bool resolved {false};
    bool has_layout {false};
    std::vector<ResourceBindingSnapshot> bindings;
};

struct FEI_REFLECT RenderGraphSnapshot {
    bool available {false};
    bool compiled {false};
    std::string compile_error;
    std::uint64_t total_passes {0};
    std::uint64_t active_passes {0};
    std::uint64_t culled_passes {0};
    std::uint64_t transient_texture_requests {0};
    std::uint64_t transient_texture_hits {0};
    std::uint64_t transient_texture_creates {0};
    std::size_t texture_pool_size {0};
    std::vector<std::string> active_order;
    std::vector<RenderPassSnapshot> passes;
    std::vector<TextureSnapshot> textures;
    std::vector<ResourceSetSnapshot> resource_sets;
};

struct FEI_REFLECT ResourceSetSourceSnapshot {
    std::string name;
    std::uint64_t requests {0};
    std::uint64_t hits {0};
    std::uint64_t creates {0};
    std::size_t cache_size {0};
};

struct FEI_REFLECT GraphicsCacheSnapshot {
    std::uint64_t framebuffer_requests {0};
    std::uint64_t framebuffer_hits {0};
    std::uint64_t framebuffer_creates {0};
    std::size_t framebuffer_cache_size {0};
    std::uint64_t resource_set_requests {0};
    std::uint64_t resource_set_hits {0};
    std::uint64_t resource_set_creates {0};
    std::size_t resource_set_cache_size {0};
    std::vector<ResourceSetSourceSnapshot> resource_set_sources;
};

RenderGraphSnapshot make_render_graph_snapshot(const RgDebugInfo& debug);

GraphicsCacheSnapshot
make_graphics_cache_snapshot(const GraphicsResourceCacheStats& stats);

} // namespace fei::devtools::rendering
