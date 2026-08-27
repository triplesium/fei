#include "frame_profile_history.hpp"

namespace ets::profiling_detail {

std::uint64_t FrameProfileHistory::push(std::int64_t duration_ns) {
    if (m_samples.size() >= Capacity) {
        m_samples.pop_front();
    }
    const auto frame = m_next_frame++;
    m_samples.push_back(
        FrameProfileHistorySample {
            .frame = frame,
            .duration_ns = duration_ns,
        }
    );
    return frame;
}

std::vector<FrameProfileHistorySample> FrameProfileHistory::samples() const {
    return {m_samples.begin(), m_samples.end()};
}

void FrameProfileHistory::clear() {
    m_samples.clear();
    m_next_frame = 0;
}

} // namespace ets::profiling_detail
