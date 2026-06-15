#include "web_preview/server.hpp"

#include "base/log.hpp"

#include <httplib.h>
#include <string>
#include <utility>

namespace fei {

namespace {

constexpr const char c_index_html[] = R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>fei web preview</title>
  <style>
    html, body { margin: 0; width: 100%; height: 100%; background: #111; }
    body { display: grid; place-items: center; overflow: hidden; }
    img { max-width: 100vw; max-height: 100vh; object-fit: contain; image-rendering: auto; }
  </style>
</head>
<body>
  <img id="frame" alt="fei web preview">
  <script>
    const frame = document.getElementById("frame");
    const delayMs = 100;
    function refresh() {
      frame.src = "/frame.jpg?t=" + Date.now();
    }
    frame.onload = () => setTimeout(refresh, delayMs);
    frame.onerror = () => setTimeout(refresh, delayMs);
    refresh();
  </script>
</body>
</html>)";

void install_routes(
    httplib::Server& server,
    std::shared_ptr<WebPreviewFrameCache> frame_cache
) {
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(c_index_html, "text/html; charset=utf-8");
    });

    server.Get(
        "/frame.jpg",
        [frame_cache](const httplib::Request&, httplib::Response& res) {
            auto frame = frame_cache->snapshot();
            if (frame.empty()) {
                res.status = 503;
                res.set_header("Cache-Control", "no-store");
                res.set_content("No frame captured yet", "text/plain");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(
                reinterpret_cast<const char*>(frame.jpeg.data()),
                frame.jpeg.size(),
                "image/jpeg"
            );
        }
    );
}

} // namespace

WebPreviewServer::WebPreviewServer(
    WebPreviewConfig config,
    std::shared_ptr<WebPreviewFrameCache> frame_cache
) :
    m_config(std::move(config)), m_frame_cache(std::move(frame_cache)),
    m_server(std::make_unique<httplib::Server>()) {
    install_routes(*m_server, m_frame_cache);
}

WebPreviewServer::~WebPreviewServer() {
    stop();
}

WebPreviewServer::WebPreviewServer(WebPreviewServer&& other) noexcept :
    m_config(std::move(other.m_config)),
    m_frame_cache(std::move(other.m_frame_cache)),
    m_server(std::move(other.m_server)), m_thread(std::move(other.m_thread)) {}

WebPreviewServer&
WebPreviewServer::operator=(WebPreviewServer&& other) noexcept {
    if (this != &other) {
        stop();
        m_config = std::move(other.m_config);
        m_frame_cache = std::move(other.m_frame_cache);
        m_server = std::move(other.m_server);
        m_thread = std::move(other.m_thread);
    }
    return *this;
}

void WebPreviewServer::start() {
    if (m_thread.joinable()) {
        return;
    }

    auto* server = m_server.get();
    auto host = m_config.host;
    auto port = m_config.port;

    m_thread = std::thread([server, host = std::move(host), port]() {
        info("Web preview listening at http://{}:{}", host, port);
        if (!server->listen(host, port)) {
            error("Web preview failed to listen at {}:{}", host, port);
        }
    });
}

void WebPreviewServer::stop() {
    if (m_server) {
        m_server->stop();
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

} // namespace fei
