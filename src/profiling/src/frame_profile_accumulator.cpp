#include "frame_profile_accumulator.hpp"

namespace fei::profiling_detail {

namespace {

double nanoseconds_to_milliseconds(std::int64_t duration_ns) {
    return static_cast<double>(duration_ns) / 1'000'000.0;
}

} // namespace

std::optional<std::int64_t> FrameProfileAccumulator::mark(std::int64_t now_ns) {
    if (m_last_mark_ns == 0) {
        m_last_mark_ns = now_ns;
        return std::nullopt;
    }

    const auto duration_ns = now_ns - m_last_mark_ns;
    m_last_mark_ns = now_ns;
    if (duration_ns <= 0) {
        return std::nullopt;
    }

    ++m_frame_count;
    ++m_window_frame_count;
    m_window_duration_ns += duration_ns;
    m_latest_frame_ms = nanoseconds_to_milliseconds(duration_ns);

    if (m_window_duration_ns >= WindowDurationNs) {
        m_fps = static_cast<double>(m_window_frame_count) * 1'000'000'000.0 /
                static_cast<double>(m_window_duration_ns);
        m_average_frame_ms = nanoseconds_to_milliseconds(
            m_window_duration_ns /
            static_cast<std::int64_t>(m_window_frame_count)
        );
        m_window_frame_count = 0;
        m_window_duration_ns = 0;
    }

    return duration_ns;
}

FrameProfileStats FrameProfileAccumulator::stats() const {
    auto fps = m_fps;
    auto average_frame_ms = m_average_frame_ms;
    if (fps == 0.0 && m_window_frame_count > 0 && m_window_duration_ns > 0) {
        fps = static_cast<double>(m_window_frame_count) * 1'000'000'000.0 /
              static_cast<double>(m_window_duration_ns);
        average_frame_ms = nanoseconds_to_milliseconds(
            m_window_duration_ns /
            static_cast<std::int64_t>(m_window_frame_count)
        );
    }
    return FrameProfileStats {
        .frame_count = m_frame_count,
        .fps = fps,
        .latest_frame_ms = m_latest_frame_ms,
        .average_frame_ms = average_frame_ms,
    };
}

void FrameProfileAccumulator::clear() {
    m_last_mark_ns = 0;
    m_frame_count = 0;
    m_window_frame_count = 0;
    m_window_duration_ns = 0;
    m_fps = 0.0;
    m_average_frame_ms = 0.0;
    m_latest_frame_ms = 0.0;
}

} // namespace fei::profiling_detail
