#include "web_preview/frame_cache.hpp"

#include <chrono>
#include <utility>

namespace fei {

namespace {

void update_smoothed_fps(
    float& fps,
    std::chrono::steady_clock::time_point& previous_at,
    std::chrono::steady_clock::time_point now
) {
    if (previous_at != std::chrono::steady_clock::time_point {}) {
        auto seconds = std::chrono::duration<float>(now - previous_at).count();
        if (seconds > 0.0f) {
            auto instant_fps = 1.0f / seconds;
            fps = fps == 0.0f ? instant_fps : fps * 0.9f + instant_fps * 0.1f;
        }
    }
    previous_at = now;
}

} // namespace

void WebPreviewFrameCache::mark_frame_tick() {
    std::scoped_lock lock(m_mutex);
    update_smoothed_fps(
        m_engine_fps,
        m_previous_frame_tick_at,
        std::chrono::steady_clock::now()
    );
    m_engine_fps_source = "render_last_tick";
}

void WebPreviewFrameCache::publish_jpeg(
    std::vector<byte> jpeg,
    uint32 width,
    uint32 height,
    std::string target
) {
    std::scoped_lock lock(m_mutex);
    auto captured_at = std::chrono::steady_clock::now();
    update_smoothed_fps(m_capture_fps, m_previous_capture_at, captured_at);

    m_frame.jpeg = std::move(jpeg);
    m_frame.width = width;
    m_frame.height = height;
    m_frame.target = std::move(target);
    m_frame.captured_at = captured_at;
    ++m_frame.index;
    m_last_error.clear();
}

void WebPreviewFrameCache::report_failure(std::string error) {
    std::scoped_lock lock(m_mutex);
    m_last_error = std::move(error);
}

WebPreviewFrame WebPreviewFrameCache::snapshot() const {
    std::scoped_lock lock(m_mutex);
    return m_frame;
}

WebPreviewStatus WebPreviewFrameCache::status() const {
    std::scoped_lock lock(m_mutex);
    WebPreviewStatus status {
        .has_frame = !m_frame.empty(),
        .width = m_frame.width,
        .height = m_frame.height,
        .frame_index = m_frame.index,
        .capture_fps = m_capture_fps,
        .engine_fps = m_engine_fps,
        .engine_fps_source = m_engine_fps_source,
        .jpeg_bytes = m_frame.jpeg.size(),
        .target = m_frame.target,
        .last_error = m_last_error,
    };
    if (!m_frame.empty()) {
        auto age = std::chrono::steady_clock::now() - m_frame.captured_at;
        status.frame_age_ms = static_cast<uint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(age).count()
        );
    }
    return status;
}

void WebPreviewFrameCache::clear() {
    std::scoped_lock lock(m_mutex);
    m_frame = {};
    m_previous_capture_at = {};
    m_previous_frame_tick_at = {};
    m_capture_fps = 0.0f;
    m_engine_fps = 0.0f;
    m_engine_fps_source.clear();
    m_last_error.clear();
}

} // namespace fei
