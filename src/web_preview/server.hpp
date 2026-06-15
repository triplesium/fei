#pragma once
#include "web_preview/frame_cache.hpp"
#include "web_preview/frame_encoder.hpp"
#include "web_preview/plugin.hpp"
#include "web_preview/web_input.hpp"

#include <memory>
#include <thread>

namespace httplib {
class Server;
}

namespace fei {

class WebPreviewServer {
  public:
    explicit WebPreviewServer(WebPreviewConfig config);
    ~WebPreviewServer();

    WebPreviewServer(const WebPreviewServer&) = delete;
    WebPreviewServer& operator=(const WebPreviewServer&) = delete;
    WebPreviewServer(WebPreviewServer&& other) noexcept;
    WebPreviewServer& operator=(WebPreviewServer&& other) noexcept;

    void start();
    void stop();

    const WebPreviewConfig& config() const { return m_config; }
    std::shared_ptr<WebPreviewFrameCache> frame_cache() const {
        return m_frame_cache;
    }
    std::shared_ptr<WebPreviewInput> input() const { return m_input; }
    bool can_accept_frame() const;
    bool submit_frame(WebPreviewEncodeJob job);

  private:
    WebPreviewConfig m_config;
    std::shared_ptr<WebPreviewFrameCache> m_frame_cache;
    std::shared_ptr<WebPreviewInput> m_input;
    std::unique_ptr<WebPreviewFrameEncoder> m_encoder;
    std::unique_ptr<httplib::Server> m_server;
    std::thread m_thread;
};

} // namespace fei
