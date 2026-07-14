#include "frame_profile_history.hpp"

namespace fei::profiling_detail {

void FrameProfileHistory::push(std::int64_t duration_ns) {
    if (m_samples.size() >= Capacity) {
        m_samples.pop_front();
    }
    m_samples.push_back(
        FrameProfileHistorySample {
            .frame = m_next_frame++,
            .duration_ns = duration_ns,
        }
    );
}

std::vector<FrameProfileHistorySample> FrameProfileHistory::samples() const {
    return {m_samples.begin(), m_samples.end()};
}

void FrameProfileHistory::clear() {
    m_samples.clear();
    m_next_frame = 0;
}

} // namespace fei::profiling_detail
