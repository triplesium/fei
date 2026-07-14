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

namespace {

SummaryEntrySnapshot make_summary_entry(const ProfileEntrySnapshot& source) {
    return SummaryEntrySnapshot {
        .schedule_id = source.schedule_id,
        .schedule_name = source.schedule_name,
        .name = source.name,
        .file = source.file,
        .function = source.function,
        .line = source.line,
        .count = source.count,
        .total_ms = source.total_ms,
        .self_ms = source.self_ms,
        .mean_ms = source.mean_ms,
        .self_mean_ms = source.self_mean_ms,
        .min_ms = source.min_ms,
        .max_ms = source.max_ms,
    };
}

} // namespace

SummarySnapshot
make_summary_snapshot(const fei::ProfileSummarySnapshot& source) {
    SummarySnapshot snapshot {
        .available = source.available,
        .frame_stats = make_frame_stats_snapshot(source.frame_stats),
    };
    snapshot.systems.reserve(source.systems.size());
    for (const auto& system : source.systems) {
        snapshot.systems.push_back(make_summary_entry(system));
    }
    snapshot.zones.reserve(source.zones.size());
    for (const auto& zone : source.zones) {
        snapshot.zones.push_back(make_summary_entry(zone));
    }
    return snapshot;
}

FrameHistorySnapshot
make_frame_history_snapshot(const fei::ProfileSummarySnapshot& source) {
    FrameHistorySnapshot snapshot {
        .available = source.available,
    };
    snapshot.frames.reserve(source.frames.size());
    for (const auto& frame : source.frames) {
        snapshot.frames.push_back(
            FrameHistorySampleSnapshot {
                .frame = frame.frame,
                .duration_ms = frame.duration_ms,
            }
        );
    }
    return snapshot;
}

} // namespace fei::devtools::profiling
