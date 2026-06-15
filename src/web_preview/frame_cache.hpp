#pragma once
#include "base/types.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace fei {

struct WebPreviewFrame {
    std::vector<byte> jpeg;
    uint32 width {0};
    uint32 height {0};
    uint64 index {0};
    std::string target;
    std::chrono::steady_clock::time_point captured_at;

    bool empty() const { return jpeg.empty(); }
};

struct WebPreviewStatus {
    bool has_frame {false};
    uint32 width {0};
    uint32 height {0};
    uint64 frame_index {0};
    uint64 frame_age_ms {0};
    float capture_fps {0.0f};
    float engine_fps {0.0f};
    std::size_t jpeg_bytes {0};
    std::string target;
    std::string last_error;
};

class WebPreviewFrameCache {
  public:
    void mark_frame_tick();
    void publish_jpeg(
        std::vector<byte> jpeg,
        uint32 width,
        uint32 height,
        std::string target
    );
    void report_failure(std::string error);
    WebPreviewFrame snapshot() const;
    WebPreviewFrame wait_for_frame_after(
        uint64 frame_index,
        std::chrono::milliseconds timeout
    ) const;
    WebPreviewStatus status() const;
    void clear();

  private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_frame_available;
    WebPreviewFrame m_frame;
    std::chrono::steady_clock::time_point m_previous_capture_at;
    std::chrono::steady_clock::time_point m_previous_frame_tick_at;
    float m_capture_fps {0.0f};
    float m_engine_fps {0.0f};
    std::string m_last_error;
};

} // namespace fei
