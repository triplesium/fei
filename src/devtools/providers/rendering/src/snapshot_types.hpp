#pragma once

#include "ecs/schedule.hpp"
#include "graphics/graphics_device.hpp"
#include "refl/reflect.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fei::devtools::rendering {

struct FEI_REFLECT RenderSystemSnapshot {
    std::uint32_t id {0};
    std::string name;
    std::vector<std::uint32_t> dependencies;
    std::uint64_t topological_index {0};
    std::uint64_t batch_index {0};
};

struct FEI_REFLECT RenderScheduleSnapshot {
    bool available {false};
    std::uint64_t total_systems {0};
    std::uint64_t batch_count {0};
    std::vector<RenderSystemSnapshot> systems;
    std::vector<std::vector<std::uint32_t>> batches;
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

RenderScheduleSnapshot
make_render_schedule_snapshot(const ScheduleDebugInfo& debug);

GraphicsCacheSnapshot
make_graphics_cache_snapshot(const GraphicsResourceCacheStats& stats);

} // namespace fei::devtools::rendering
