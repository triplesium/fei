#pragma once

#include "profiling/profiling.hpp"
#include "refl/reflect.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei::devtools::profiling {

FEI_REFLECT()
struct FrameStatsSnapshot {
    bool available {false};
    std::uint64_t frame_count {0};
    double fps {0.0};
    double latest_frame_ms {0.0};
    double average_frame_ms {0.0};
};

FEI_REFLECT()
struct SummaryEntrySnapshot {
    std::uint64_t schedule_id {0};
    std::string schedule_name;
    std::string name;
    std::string file;
    std::string function;
    std::uint32_t line {0};
    std::uint64_t count {0};
    double total_ms {0.0};
    double self_ms {0.0};
    double mean_ms {0.0};
    double self_mean_ms {0.0};
    double min_ms {0.0};
    double max_ms {0.0};
};

FEI_REFLECT()
struct SummarySnapshot {
    bool available {false};
    FrameStatsSnapshot frame_stats;
    std::vector<SummaryEntrySnapshot> systems;
    std::vector<SummaryEntrySnapshot> zones;
};

FEI_REFLECT()
struct FrameHistorySampleSnapshot {
    std::uint64_t frame {0};
    double duration_ms {0.0};
};

FEI_REFLECT()
struct FrameHistorySnapshot {
    bool available {false};
    std::vector<FrameHistorySampleSnapshot> frames;
};

FEI_REFLECT()
struct GpuSummaryEntrySnapshot {
    std::string name;
    std::uint64_t count {0};
    double latest_ms {0.0};
    double total_ms {0.0};
    double mean_ms {0.0};
    double min_ms {0.0};
    double max_ms {0.0};
};

FEI_REFLECT()
struct GpuSummarySnapshot {
    bool available {false};
    std::vector<GpuSummaryEntrySnapshot> entries;
};

FrameStatsSnapshot make_frame_stats_snapshot(const FrameProfileStats& stats);
SummarySnapshot
make_summary_snapshot(const fei::ProfileSummarySnapshot& source);
FrameHistorySnapshot
make_frame_history_snapshot(const fei::ProfileSummarySnapshot& source);
GpuSummarySnapshot
make_gpu_summary_snapshot(const fei::GpuProfileSummarySnapshot& source);

} // namespace fei::devtools::profiling
