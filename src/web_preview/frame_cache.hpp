#pragma once
#include "base/types.hpp"

#include <mutex>
#include <vector>

namespace fei {

struct WebPreviewFrame {
    std::vector<byte> jpeg;
    uint32 width {0};
    uint32 height {0};
    uint64 index {0};

    bool empty() const { return jpeg.empty(); }
};

class WebPreviewFrameCache {
  public:
    void publish_jpeg(std::vector<byte> jpeg, uint32 width, uint32 height);
    WebPreviewFrame snapshot() const;
    void clear();

  private:
    mutable std::mutex m_mutex;
    WebPreviewFrame m_frame;
};

} // namespace fei
