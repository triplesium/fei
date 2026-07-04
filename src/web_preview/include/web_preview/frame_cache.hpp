#pragma once
#include "base/types.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace fei {

struct WebPreviewFrame {
    std::vector<byte> jpeg;
    uint32 width {0};
    uint32 height {0};
    uint64 index {0};
    std::string target;
    std::chrono::steady_clock::time_point captured_at;

    bool empty() const { return jpeg.empty(); }
};

struct WebPreviewRenderGraphStats {
    bool compiled {false};
    std::string compile_error;
    uint64 total_passes {0};
    uint64 active_passes {0};
    uint64 culled_passes {0};
    uint64 transient_texture_requests {0};
    uint64 transient_texture_hits {0};
    uint64 transient_texture_creates {0};
    std::size_t texture_pool_size {0};
    std::vector<std::string> active_order;
};

struct WebPreviewRenderGraphTextureUse {
    uint32 index {0};
    uint32 generation {0};
    std::string name;
    std::string access;
};

struct WebPreviewRenderGraphPass {
    uint32 index {0};
    std::string name;
    bool active {false};
    bool side_effect {false};
    std::vector<uint32> dependencies;
    std::vector<WebPreviewRenderGraphTextureUse> reads;
    std::vector<WebPreviewRenderGraphTextureUse> writes;
};

struct WebPreviewRenderGraphTexture {
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
    uint32 first_active_use {0};
    uint32 last_active_use {0};
};

struct WebPreviewRenderGraphResourceSetBinding {
    uint32 index {0};
    std::string kind;
    std::string resource_name;
    bool valid {false};
    uint32 texture_index {0};
    uint32 texture_generation {0};
};

struct WebPreviewRenderGraphResourceSet {
    uint32 index {0};
    uint32 generation {0};
    uint32 pass_index {0};
    std::string name;
    bool active {false};
    bool resolved {false};
    bool has_layout {false};
    std::vector<WebPreviewRenderGraphResourceSetBinding> bindings;
};

struct WebPreviewResourceSetSourceStats {
    std::string name;
    uint64 requests {0};
    uint64 hits {0};
    uint64 creates {0};
    std::size_t cache_size {0};
};

struct WebPreviewGraphicsCacheStats {
    uint64 framebuffer_requests {0};
    uint64 framebuffer_hits {0};
    uint64 framebuffer_creates {0};
    uint64 resource_set_requests {0};
    uint64 resource_set_hits {0};
    uint64 resource_set_creates {0};
    std::size_t framebuffer_cache_size {0};
    std::size_t resource_set_cache_size {0};
    std::vector<WebPreviewResourceSetSourceStats> resource_set_sources;
};

struct WebPreviewDebugStats {
    WebPreviewRenderGraphStats render_graph;
    std::vector<WebPreviewRenderGraphPass> render_graph_passes;
    std::vector<WebPreviewRenderGraphTexture> render_graph_textures;
    std::vector<WebPreviewRenderGraphResourceSet> render_graph_resource_sets;
    WebPreviewGraphicsCacheStats graphics_cache;
};

struct WebPreviewStatus {
    bool has_frame {false};
    uint32 width {0};
    uint32 height {0};
    uint64 frame_index {0};
    uint64 frame_age_ms {0};
    float capture_fps {0.0f};
    float engine_fps {0.0f};
    std::size_t jpeg_bytes {0};
    std::string target;
    std::string last_error;
    WebPreviewDebugStats debug;
};

class WebPreviewFrameCache {
  public:
    void mark_frame_tick();
    void publish_jpeg(
        std::vector<byte> jpeg,
        uint32 width,
        uint32 height,
        std::string target
    );
    void report_failure(std::string error);
    void update_debug_stats(WebPreviewDebugStats stats);
    WebPreviewFrame snapshot() const;
    WebPreviewFrame wait_for_frame_after(
        uint64 frame_index,
        std::chrono::milliseconds timeout
    ) const;
    WebPreviewStatus status() const;
    void clear();

  private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_frame_available;
    WebPreviewFrame m_frame;
    std::chrono::steady_clock::time_point m_previous_capture_at;
    std::chrono::steady_clock::time_point m_previous_frame_tick_at;
    float m_capture_fps {0.0f};
    float m_engine_fps {0.0f};
    std::string m_last_error;
    WebPreviewDebugStats m_debug_stats;
};

} // namespace fei
