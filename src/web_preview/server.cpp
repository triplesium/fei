#include "web_preview/server.hpp"

#include "base/log.hpp"

#include <httplib.h>
#include <string>
#include <utility>

namespace fei {

namespace {

constexpr const char c_index_html[] = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>fei web preview</title>
  <style>
    html, body { margin: 0; width: 100%; height: 100%; background: #111; }
    body { display: grid; place-items: center; overflow: hidden; }
    img { max-width: 100vw; max-height: 100vh; object-fit: contain; image-rendering: auto; }
    #hud {
      position: fixed;
      top: 12px;
      left: 12px;
      min-width: 260px;
      padding: 10px 12px;
      border: 1px solid rgba(255, 255, 255, 0.16);
      border-radius: 6px;
      background: rgba(0, 0, 0, 0.64);
      color: #e8e8e8;
      font: 12px/1.45 ui-monospace, SFMono-Regular, Consolas, "Liberation Mono", monospace;
      pointer-events: none;
      white-space: pre;
    }
    #hud .error { color: #ffb4a8; }
  </style>
</head>
<body>
  <img id="frame" alt="fei web preview">
  <div id="hud">waiting for status...</div>
  <script>
    const frame = document.getElementById("frame");
    const hud = document.getElementById("hud");
    const delayMs = 100;
    function refresh() {
      frame.src = "/frame.jpg?t=" + Date.now();
    }
    frame.onload = () => setTimeout(refresh, delayMs);
    frame.onerror = () => setTimeout(refresh, delayMs);
    async function refreshStatus() {
      try {
        const status = await fetch("/status.json?t=" + Date.now(), { cache: "no-store" }).then(r => r.json());
        const lines = [
          "fei web preview",
          "frame: " + (status.has_frame ? status.frame_index : "none"),
          "engine fps: " + status.engine_fps.toFixed(1),
          "capture fps: " + status.capture_fps.toFixed(1),
          "target: " + (status.target || "none"),
          "size: " + (status.has_frame ? `${status.width}x${
    status.height}` : "none"),
          "jpeg: " + status.jpeg_bytes + " bytes",
          "age: " + status.frame_age_ms + " ms",
        ];
if (status.last_error) {
    lines.push("error: " + status.last_error);
    hud.innerHTML = lines.slice(0, -1).join("\n") + "\n<span class=\"error\">" +
                    lines.at(-1) + "</span>";
} else {
    hud.textContent = lines.join("\n");
}
}
catch (error) {
    hud.innerHTML =
        "fei web preview\n<span class=\"error\">status unavailable</span>";
}
}
refresh();
refreshStatus();
setInterval(refreshStatus, 500);
  </script>
</body>
</html>)HTML";

std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);
    for (auto ch : value) {
        switch (ch) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += ch;
                break;
        }
    }
    return result;
}

std::string status_to_json(const WebPreviewStatus& status) {
    std::string json;
    json.reserve(256 + status.target.size() + status.last_error.size());
    json += "{";
    json += "\"has_frame\":";
    json += status.has_frame ? "true" : "false";
    json += ",\"width\":";
    json += std::to_string(status.width);
    json += ",\"height\":";
    json += std::to_string(status.height);
    json += ",\"frame_index\":";
    json += std::to_string(status.frame_index);
    json += ",\"frame_age_ms\":";
    json += std::to_string(status.frame_age_ms);
    json += ",\"capture_fps\":";
    json += std::to_string(status.capture_fps);
    json += ",\"engine_fps\":";
    json += std::to_string(status.engine_fps);
    json += R"(,"engine_fps_source":")";
    json += json_escape(status.engine_fps_source);
    json += "\"";
    json += ",\"jpeg_bytes\":";
    json += std::to_string(status.jpeg_bytes);
    json += R"(,"target":")";
    json += json_escape(status.target);
    json += R"(","last_error":")";
    json += json_escape(status.last_error);
    json += "\"}";
    return json;
}

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

    server.Get(
        "/status.json",
        [frame_cache](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-store");
            res.set_content(
                status_to_json(frame_cache->status()),
                "application/json"
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
    m_encoder(std::make_unique<WebPreviewFrameEncoder>(m_frame_cache)),
    m_server(std::make_unique<httplib::Server>()) {
    install_routes(*m_server, m_frame_cache);
}

WebPreviewServer::~WebPreviewServer() {
    stop();
}

WebPreviewServer::WebPreviewServer(WebPreviewServer&& other) noexcept :
    m_config(std::move(other.m_config)),
    m_frame_cache(std::move(other.m_frame_cache)),
    m_encoder(std::move(other.m_encoder)), m_server(std::move(other.m_server)),
    m_thread(std::move(other.m_thread)) {}

WebPreviewServer&
WebPreviewServer::operator=(WebPreviewServer&& other) noexcept {
    if (this != &other) {
        stop();
        m_config = std::move(other.m_config);
        m_frame_cache = std::move(other.m_frame_cache);
        m_encoder = std::move(other.m_encoder);
        m_server = std::move(other.m_server);
        m_thread = std::move(other.m_thread);
    }
    return *this;
}

void WebPreviewServer::start() {
    if (m_thread.joinable()) {
        return;
    }

    if (m_encoder) {
        m_encoder->start();
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
    if (m_encoder) {
        m_encoder->stop();
    }
}

bool WebPreviewServer::can_accept_frame() const {
    return m_encoder && m_encoder->can_accept_frame();
}

bool WebPreviewServer::submit_frame(WebPreviewEncodeJob job) {
    return m_encoder && m_encoder->submit(std::move(job));
}

} // namespace fei
