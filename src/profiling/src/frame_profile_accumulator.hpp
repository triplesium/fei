#pragma once

#include "profiling/profiling.hpp"

#include <cstdint>
#include <optional>

namespace fei::profiling_detail {

class FrameProfileAccumulator {
  private:
    static constexpr std::int64_t WindowDurationNs = 500'000'000;

    std::int64_t m_last_mark_ns {0};
    std::uint64_t m_frame_count {0};
    std::uint64_t m_window_frame_count {0};
    std::int64_t m_window_duration_ns {0};
    double m_fps {0.0};
    double m_average_frame_ms {0.0};
    double m_latest_frame_ms {0.0};

  public:
    [[nodiscard]] std::optional<std::int64_t> mark(std::int64_t now_ns);
    [[nodiscard]] FrameProfileStats stats() const;
    void clear();
};

} // namespace fei::profiling_detail
