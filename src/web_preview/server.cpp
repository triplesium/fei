#include "web_preview/server.hpp"

#include "asset/embed.hpp"
#include "base/log.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <httplib.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

EMBED(index_html, "index.html");

namespace fei {

namespace {

constexpr std::string_view c_mjpeg_boundary {"fei-frame"};

std::string_view index_html() {
    auto reader = EmbededAssets::get("index.html").reader();
    auto html = reader.as_string_view();
    if (!html.empty() && html.back() == '\0') {
        html.remove_suffix(1);
    }
    return html;
}

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
    json.reserve(192 + status.target.size() + status.last_error.size());
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
    json += ",\"jpeg_bytes\":";
    json += std::to_string(status.jpeg_bytes);
    json += R"(,"target":")";
    json += json_escape(status.target);
    json += R"(","last_error":")";
    json += json_escape(status.last_error);
    json += "\"}";
    return json;
}

std::optional<int> parse_int(const std::string& value) {
    int parsed {};
    auto* begin = value.data();
    auto* end = begin + value.size();
    auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc {} || ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> parse_bool_param(const std::string& value) {
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    return std::nullopt;
}

bool write_sink(httplib::DataSink& sink, std::string_view content) {
    return sink.write(content.data(), content.size());
}

bool write_mjpeg_frame(httplib::DataSink& sink, const WebPreviewFrame& frame) {
    std::string header;
    header.reserve(128);
    header += "--";
    header += c_mjpeg_boundary;
    header += "\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    header += std::to_string(frame.jpeg.size());
    header += "\r\nX-Frame-Index: ";
    header += std::to_string(frame.index);
    header += "\r\n\r\n";

    return write_sink(sink, header) &&
           sink.write(
               reinterpret_cast<const char*>(frame.jpeg.data()),
               frame.jpeg.size()
           ) &&
           write_sink(sink, "\r\n");
}

void install_routes(
    httplib::Server& server,
    std::shared_ptr<WebPreviewFrameCache> frame_cache,
    std::shared_ptr<WebPreviewInput> input,
    bool handle_input
) {
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        auto html = index_html();
        res.set_content(html.data(), html.size(), "text/html; charset=utf-8");
    });

    server.Get(
        "/stream.mjpg",
        [frame_cache](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-store");
            res.set_chunked_content_provider(
                "multipart/x-mixed-replace; boundary=fei-frame",
                [frame_cache,
                 frame_index =
                     uint64 {0}](std::size_t, httplib::DataSink& sink) mutable {
                    if (!sink.is_writable()) {
                        return false;
                    }

                    auto frame = frame_cache->wait_for_frame_after(
                        frame_index,
                        std::chrono::milliseconds {1000}
                    );
                    if (frame.empty()) {
                        return sink.is_writable();
                    }

                    frame_index = frame.index;
                    return write_mjpeg_frame(sink, frame);
                }
            );
        }
    );

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

    if (handle_input) {
        server.Post(
            "/input/key",
            [input](const httplib::Request& req, httplib::Response& res) {
                res.set_header("Cache-Control", "no-store");
                if (!req.has_param("key") || !req.has_param("down")) {
                    res.status = 400;
                    res.set_content(
                        "Missing key or down parameter",
                        "text/plain"
                    );
                    return;
                }

                auto key = parse_int(req.get_param_value("key"));
                auto down = parse_bool_param(req.get_param_value("down"));
                if (!key || !down) {
                    res.status = 400;
                    res.set_content(
                        "Invalid key or down parameter",
                        "text/plain"
                    );
                    return;
                }

                if (!input->set_key(static_cast<KeyCode>(*key), *down)) {
                    res.status = 400;
                    res.set_content("Unsupported key code", "text/plain");
                    return;
                }

                res.set_content("ok", "text/plain");
            }
        );

        server.Post(
            "/input/clear",
            [input](const httplib::Request&, httplib::Response& res) {
                input->clear();
                res.set_header("Cache-Control", "no-store");
                res.set_content("ok", "text/plain");
            }
        );
    }
}

} // namespace

WebPreviewServer::WebPreviewServer(WebPreviewConfig config) :
    m_config(std::move(config)),
    m_frame_cache(std::make_shared<WebPreviewFrameCache>()),
    m_input(std::make_shared<WebPreviewInput>()),
    m_encoder(std::make_unique<WebPreviewFrameEncoder>(m_frame_cache)),
    m_server(std::make_unique<httplib::Server>()) {
    install_routes(*m_server, m_frame_cache, m_input, m_config.handle_input);
}

WebPreviewServer::~WebPreviewServer() {
    stop();
}

WebPreviewServer::WebPreviewServer(WebPreviewServer&& other) noexcept :
    m_config(std::move(other.m_config)),
    m_frame_cache(std::move(other.m_frame_cache)),
    m_input(std::move(other.m_input)), m_encoder(std::move(other.m_encoder)),
    m_server(std::move(other.m_server)), m_thread(std::move(other.m_thread)) {}

WebPreviewServer&
WebPreviewServer::operator=(WebPreviewServer&& other) noexcept {
    if (this != &other) {
        stop();
        m_config = std::move(other.m_config);
        m_frame_cache = std::move(other.m_frame_cache);
        m_input = std::move(other.m_input);
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
