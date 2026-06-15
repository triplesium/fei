#include "web_preview/frame_cache.hpp"

#include <utility>

namespace fei {

void WebPreviewFrameCache::publish_jpeg(
    std::vector<byte> jpeg,
    uint32 width,
    uint32 height
) {
    std::scoped_lock lock(m_mutex);
    m_frame.jpeg = std::move(jpeg);
    m_frame.width = width;
    m_frame.height = height;
    ++m_frame.index;
}

WebPreviewFrame WebPreviewFrameCache::snapshot() const {
    std::scoped_lock lock(m_mutex);
    return m_frame;
}

void WebPreviewFrameCache::clear() {
    std::scoped_lock lock(m_mutex);
    m_frame = {};
}

} // namespace fei
