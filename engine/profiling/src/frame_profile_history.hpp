#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace fei::profiling_detail {

struct FrameProfileHistorySample {
    std::uint64_t frame {0};
    std::int64_t duration_ns {0};
};

class FrameProfileHistory {
  public:
    static constexpr std::size_t Capacity = 600;

    void push(std::int64_t duration_ns);
    [[nodiscard]] std::vector<FrameProfileHistorySample> samples() const;
    void clear();

  private:
    std::deque<FrameProfileHistorySample> m_samples;
    std::uint64_t m_next_frame {0};
};

} // namespace fei::profiling_detail
