#pragma once

#include "profiling/profiling.hpp"
#include "refl/reflect.hpp"

#include <cstdint>

namespace fei::devtools::profiling {

struct FEI_REFLECT FrameStatsSnapshot {
    bool available {false};
    std::uint64_t frame_count {0};
    double fps {0.0};
    double latest_frame_ms {0.0};
    double average_frame_ms {0.0};
};

FrameStatsSnapshot make_frame_stats_snapshot(const FrameProfileStats& stats);

} // namespace fei::devtools::profiling
