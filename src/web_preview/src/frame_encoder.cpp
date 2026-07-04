#include "web_preview/frame_encoder.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fei {

namespace {

void append_jpeg_bytes(void* context, void* data, int size) {
    auto& bytes = *static_cast<std::vector<byte>*>(context);
    auto* first = static_cast<byte*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

std::vector<unsigned char> rgba_to_flipped_rgb(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height
) {
    std::vector<unsigned char> rgb(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3
    );
    auto row_rgba_size = static_cast<std::size_t>(width) * 4;
    auto row_rgb_size = static_cast<std::size_t>(width) * 3;

    for (uint32 dst_y = 0; dst_y < height; ++dst_y) {
        auto src_y = height - dst_y - 1;
        auto* src = reinterpret_cast<const unsigned char*>(rgba.data()) +
                    static_cast<std::size_t>(src_y) * row_rgba_size;
        auto* dst = rgb.data() + static_cast<std::size_t>(dst_y) * row_rgb_size;
        for (uint32 x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    return rgb;
}

} // namespace

WebPreviewFrameEncoder::WebPreviewFrameEncoder(
    std::shared_ptr<WebPreviewFrameCache> frame_cache
) : m_frame_cache(std::move(frame_cache)) {}

WebPreviewFrameEncoder::~WebPreviewFrameEncoder() {
    stop();
}

void WebPreviewFrameEncoder::start() {
    std::scoped_lock lock(m_mutex);
    if (m_thread.joinable()) {
        return;
    }

    m_stopping = false;
    m_thread = std::thread([this]() {
        worker_loop();
    });
}

void WebPreviewFrameEncoder::stop() {
    {
        std::scoped_lock lock(m_mutex);
        m_stopping = true;
        m_pending_job.reset();
    }
    m_job_available.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool WebPreviewFrameEncoder::can_accept_frame() const {
    std::scoped_lock lock(m_mutex);
    return !m_stopping && !m_encoding && !m_pending_job.has_value();
}

bool WebPreviewFrameEncoder::submit(WebPreviewEncodeJob job) {
    {
        std::scoped_lock lock(m_mutex);
        if (m_stopping || m_encoding || m_pending_job.has_value()) {
            return false;
        }
        m_pending_job = std::move(job);
    }
    m_job_available.notify_one();
    return true;
}

void WebPreviewFrameEncoder::worker_loop() {
    while (true) {
        std::optional<WebPreviewEncodeJob> job;
        {
            std::unique_lock lock(m_mutex);
            m_job_available.wait(lock, [this]() {
                return m_stopping || m_pending_job.has_value();
            });
            if (m_stopping) {
                return;
            }
            job = std::move(m_pending_job);
            m_pending_job.reset();
            m_encoding = true;
        }

        encode_and_publish(std::move(*job));

        {
            std::scoped_lock lock(m_mutex);
            m_encoding = false;
        }
    }
}

void WebPreviewFrameEncoder::encode_and_publish(WebPreviewEncodeJob job) {
    if (job.rgba.empty()) {
        m_frame_cache->report_failure("No RGBA pixels were submitted");
        return;
    }

    auto rgb = rgba_to_flipped_rgb(job.rgba, job.width, job.height);
    std::vector<byte> jpeg;
    auto quality = std::clamp(job.jpeg_quality, 1, 100);
    auto ok = stbi_write_jpg_to_func(
        append_jpeg_bytes,
        &jpeg,
        static_cast<int>(job.width),
        static_cast<int>(job.height),
        3,
        rgb.data(),
        quality
    );
    if (ok == 0) {
        m_frame_cache->report_failure("JPEG encoding failed");
        return;
    }

    m_frame_cache->publish_jpeg(
        std::move(jpeg),
        job.width,
        job.height,
        std::move(job.target)
    );
}

} // namespace fei
