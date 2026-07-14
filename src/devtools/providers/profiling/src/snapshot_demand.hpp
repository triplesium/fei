#pragma once

#include "devtools/types.hpp"

#include <string_view>

namespace fei::devtools::profiling {

inline constexpr const char* c_frame_stats_capability = "profiling.frame_stats";
inline constexpr const char* c_summary_capability = "profiling.summary";
inline constexpr const char* c_frame_history_capability =
    "profiling.frame_history";
inline constexpr PublishMode c_summary_mode = PublishMode::Cached;
inline constexpr PublishMode c_frame_history_mode = PublishMode::OnDemand;

struct SnapshotDemand {
    bool frame_stats {false};
    bool summary {false};
    bool frame_history {false};

    void include(std::string_view capability) {
        frame_stats |= capability == c_frame_stats_capability;
        summary |= capability == c_summary_capability;
        frame_history |= capability == c_frame_history_capability;
    }

    [[nodiscard]] bool detailed() const { return summary || frame_history; }
    [[nodiscard]] bool any() const {
        return frame_stats || summary || frame_history;
    }
};

} // namespace fei::devtools::profiling
