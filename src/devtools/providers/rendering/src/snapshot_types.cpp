#include "snapshot_types.hpp"

namespace fei::devtools::rendering {

RenderScheduleSnapshot
make_render_schedule_snapshot(const ScheduleDebugInfo& debug) {
    RenderScheduleSnapshot snapshot {
        .available = true,
        .total_systems = debug.systems.size(),
        .batch_count = debug.batches.size(),
        .batches = debug.batches,
    };
    snapshot.systems.reserve(debug.systems.size());
    for (const auto& system : debug.systems) {
        snapshot.systems.push_back(
            RenderSystemSnapshot {
                .id = system.id,
                .name = system.name,
                .dependencies = system.dependencies,
                .topological_index = system.topological_index,
                .batch_index = system.batch_index,
            }
        );
    }
    return snapshot;
}

GraphicsCacheSnapshot
make_graphics_cache_snapshot(const GraphicsResourceCacheStats& stats) {
    GraphicsCacheSnapshot snapshot {
        .framebuffer_requests = stats.framebuffer_requests,
        .framebuffer_hits = stats.framebuffer_hits,
        .framebuffer_creates = stats.framebuffer_creates,
        .framebuffer_cache_size = stats.framebuffer_cache_size,
        .resource_set_requests = stats.resource_set_requests,
        .resource_set_hits = stats.resource_set_hits,
        .resource_set_creates = stats.resource_set_creates,
        .resource_set_cache_size = stats.resource_set_cache_size,
    };
    snapshot.resource_set_sources.reserve(stats.resource_set_sources.size());
    for (const auto& source : stats.resource_set_sources) {
        snapshot.resource_set_sources.push_back(
            ResourceSetSourceSnapshot {
                .name = source.name,
                .requests = source.requests,
                .hits = source.hits,
                .creates = source.creates,
                .cache_size = source.cache_size,
            }
        );
    }
    return snapshot;
}

} // namespace fei::devtools::rendering
