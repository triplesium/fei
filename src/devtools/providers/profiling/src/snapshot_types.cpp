#include "snapshot_types.hpp"

namespace fei::devtools::profiling {

FrameStatsSnapshot make_frame_stats_snapshot(const FrameProfileStats& stats) {
    return FrameStatsSnapshot {
        .available = stats.frame_count > 0,
        .frame_count = stats.frame_count,
        .fps = stats.fps,
        .latest_frame_ms = stats.latest_frame_ms,
        .average_frame_ms = stats.average_frame_ms,
    };
}

} // namespace fei::devtools::profiling
