#pragma once

#include "base/types.hpp"
#include "web_preview/frame_cache.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fei {

struct WebPreviewEncodeJob {
    std::vector<byte> rgba;
    uint32 width {0};
    uint32 height {0};
    int jpeg_quality {80};
    std::string target;
};

class WebPreviewFrameEncoder {
  public:
    explicit WebPreviewFrameEncoder(
        std::shared_ptr<WebPreviewFrameCache> frame_cache
    );
    ~WebPreviewFrameEncoder();

    WebPreviewFrameEncoder(const WebPreviewFrameEncoder&) = delete;
    WebPreviewFrameEncoder& operator=(const WebPreviewFrameEncoder&) = delete;
    WebPreviewFrameEncoder(WebPreviewFrameEncoder&&) = delete;
    WebPreviewFrameEncoder& operator=(WebPreviewFrameEncoder&&) = delete;

    void start();
    void stop();

    bool can_accept_frame() const;
    bool submit(WebPreviewEncodeJob job);

  private:
    void worker_loop();
    void encode_and_publish(WebPreviewEncodeJob job);

    std::shared_ptr<WebPreviewFrameCache> m_frame_cache;
    mutable std::mutex m_mutex;
    std::condition_variable m_job_available;
    std::optional<WebPreviewEncodeJob> m_pending_job;
    std::thread m_thread;
    bool m_encoding {false};
    bool m_stopping {false};
};

} // namespace fei
