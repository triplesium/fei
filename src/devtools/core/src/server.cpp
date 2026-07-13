#include "devtools/server.hpp"

#include "base/log.hpp"
#include "ui_assets.hpp"

#include <charconv>
#include <chrono>
#include <httplib.h>
#include <string>
#include <string_view>
#include <utility>

namespace fei::devtools {

namespace {

constexpr std::string_view c_mjpeg_boundary {"fei-frame"};
constexpr std::chrono::milliseconds c_default_timeout {5000};
struct RequestParams {
    uint64 after {0};
    bool fresh {false};
    std::chrono::milliseconds timeout {c_default_timeout};
};

bool write_sink(httplib::DataSink& sink, std::string_view content) {
    return sink.write(content.data(), content.size());
}

bool write_mjpeg_blob(httplib::DataSink& sink, const BridgeBlob& blob) {
    auto metadata = blob_metadata_json(blob);
    std::string header;
    header.reserve(256 + metadata.size());
    header += "--";
    header += c_mjpeg_boundary;
    header += "\r\nContent-Type: ";
    header += blob.mime.empty() ? "image/jpeg" : blob.mime;
    header += "\r\nContent-Length: ";
    header += std::to_string(blob.bytes.size());
    header += "\r\nX-DevTools-Capability: ";
    header += blob.capability;
    header += "\r\nX-DevTools-Version: ";
    header += std::to_string(blob.version);
    header += "\r\nX-DevTools-Metadata: ";
    header += metadata;
    header += "\r\n\r\n";

    return write_sink(sink, header) &&
           sink.write(
               reinterpret_cast<const char*>(blob.bytes.data()),
               blob.bytes.size()
           ) &&
           write_sink(sink, "\r\n");
}

Optional<uint64> parse_u64_value(std::string_view value) {
    uint64 parsed {};
    auto* begin = value.data();
    auto* end = begin + value.size();
    auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc {} || ptr != end) {
        return nullopt;
    }
    return parsed;
}

Optional<std::chrono::milliseconds>
parse_timeout_value(std::string_view value) {
    int64 parsed {};
    auto* begin = value.data();
    auto* end = begin + value.size();
    auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc {} || ptr != end || parsed < 0) {
        return nullopt;
    }
    return std::chrono::milliseconds {parsed};
}

Optional<bool> parse_bool_value(std::string_view value) {
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    return nullopt;
}

bool parse_after_param(
    const httplib::Request& req,
    RequestParams& params,
    std::string& error_message
) {
    if (!req.has_param("after")) {
        return true;
    }
    auto parsed = parse_u64_value(req.get_param_value("after"));
    if (!parsed) {
        error_message = "Invalid after parameter";
        return false;
    }
    params.after = *parsed;
    return true;
}

bool parse_fresh_param(
    const httplib::Request& req,
    RequestParams& params,
    std::string& error_message
) {
    if (!req.has_param("fresh")) {
        return true;
    }
    auto parsed = parse_bool_value(req.get_param_value("fresh"));
    if (!parsed) {
        error_message = "Invalid fresh parameter";
        return false;
    }
    params.fresh = *parsed;
    return true;
}

bool parse_timeout_param(
    const httplib::Request& req,
    RequestParams& params,
    std::string& error_message
) {
    if (!req.has_param("timeout_ms")) {
        return true;
    }
    auto parsed = parse_timeout_value(req.get_param_value("timeout_ms"));
    if (!parsed) {
        error_message = "Invalid timeout_ms parameter";
        return false;
    }
    params.timeout = *parsed;
    return true;
}

bool parse_wait_params(
    const httplib::Request& req,
    RequestParams& params,
    std::string& error_message
) {
    return parse_after_param(req, params, error_message) &&
           parse_fresh_param(req, params, error_message) &&
           parse_timeout_param(req, params, error_message);
}

bool parse_command_params(
    const httplib::Request& req,
    RequestParams& params,
    std::string& error_message
) {
    return parse_timeout_param(req, params, error_message);
}

void set_text(
    httplib::Response& res,
    int status,
    std::string_view content,
    std::string_view content_type = "application/json"
) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(content.data(), content.size(), std::string(content_type));
}

void set_ui_security_headers(httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
    res.set_header(
        "Content-Security-Policy",
        std::string(detail::c_ui_content_security_policy)
    );
    res.set_header("Referrer-Policy", "no-referrer");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
}

void serve_ui_asset(std::string_view path, httplib::Response& res) {
    auto asset = detail::find_ui_asset(path);
    if (!asset) {
        set_ui_security_headers(res);
        set_text(res, 404, "Not Found", "text/plain; charset=utf-8");
        return;
    }

    set_ui_security_headers(res);
    res.status = 200;
    res.set_content(
        asset->content.data(),
        asset->content.size(),
        std::string(asset->content_type)
    );
}

void set_bridge_response(
    httplib::Response& res,
    const BridgeResponse& response
) {
    res.status = response.status;
    res.set_header("Cache-Control", "no-store");
    for (const auto& [name, value] : response.headers) {
        res.set_header(name, value);
    }
    if (response.binary) {
        res.set_content(
            reinterpret_cast<const char*>(response.bytes.data()),
            response.bytes.size(),
            response.content_type
        );
    } else {
        res.set_content(response.text, response.content_type);
    }
}

void set_blob_response(httplib::Response& res, const BridgeBlob& blob) {
    set_bridge_response(
        res,
        BridgeResponse {
            .status = 200,
            .content_type = blob.mime,
            .headers =
                {
                    {"X-DevTools-Capability", blob.capability},
                    {"X-DevTools-Version", std::to_string(blob.version)},
                    {"X-DevTools-Metadata", blob_metadata_json(blob)},
                },
            .bytes = blob.bytes,
            .binary = true,
        }
    );
}

Optional<ManifestEntry> validate_capability(
    Bridge bridge,
    const std::string& capability,
    std::string_view expected_kind,
    httplib::Response& res
) {
    auto entry = bridge.find_capability(capability);
    if (!entry) {
        set_text(
            res,
            404,
            error_json(
                "Unknown capability",
                404,
                capability,
                std::string(expected_kind)
            )
        );
        return nullopt;
    }

    if (entry->kind != expected_kind) {
        set_text(
            res,
            409,
            error_json(
                "Capability kind mismatch: expected " +
                    std::string(expected_kind) + ", got " + entry->kind,
                409,
                capability,
                std::string(expected_kind)
            )
        );
        return nullopt;
    }

    return entry;
}

void serve_blob_request(
    Bridge bridge,
    std::string capability,
    const httplib::Request& req,
    httplib::Response& res
) {
    const auto capability_id = capability;
    if (!validate_capability(bridge, capability, "blob", res)) {
        return;
    }

    RequestParams params;
    std::string error_message;
    if (!parse_wait_params(req, params, error_message)) {
        set_text(res, 400, error_json(error_message, 400, capability, "blob"));
        return;
    }

    if (auto cached =
            bridge.cached_blob(capability, params.after, params.fresh)) {
        set_blob_response(res, *cached);
        return;
    }

    auto token = bridge.enqueue_blob_request(
        std::move(capability),
        params.after,
        params.fresh,
        params.timeout
    );
    auto response = bridge.wait_for_response(token, params.timeout);
    if (!response) {
        set_text(
            res,
            504,
            error_json("Timed out waiting for blob", 504, capability_id, "blob")
        );
        return;
    }
    set_bridge_response(res, *response);
}

void serve_snapshot_request(
    Bridge bridge,
    std::string capability,
    const httplib::Request& req,
    httplib::Response& res
) {
    const auto capability_id = capability;
    if (!validate_capability(bridge, capability, "snapshot", res)) {
        return;
    }

    RequestParams params;
    std::string error_message;
    if (!parse_wait_params(req, params, error_message)) {
        set_text(
            res,
            400,
            error_json(error_message, 400, capability, "snapshot")
        );
        return;
    }

    if (auto cached =
            bridge.cached_snapshot(capability, params.after, params.fresh)) {
        set_text(res, 200, snapshot_envelope_json(*cached));
        return;
    }

    auto token = bridge.enqueue_snapshot_request(
        std::move(capability),
        params.after,
        params.fresh,
        params.timeout
    );
    auto response = bridge.wait_for_response(token, params.timeout);
    if (!response) {
        set_text(
            res,
            504,
            error_json(
                "Timed out waiting for snapshot",
                504,
                capability_id,
                "snapshot"
            )
        );
        return;
    }
    set_bridge_response(res, *response);
}

void serve_command_request(
    Bridge bridge,
    std::string capability,
    std::string body,
    const httplib::Request& req,
    httplib::Response& res
) {
    const auto capability_id = capability;
    if (!validate_capability(bridge, capability, "command", res)) {
        return;
    }

    RequestParams params;
    std::string error_message;
    if (!parse_command_params(req, params, error_message)) {
        set_text(
            res,
            400,
            error_json(error_message, 400, capability, "command")
        );
        return;
    }

    if (body.empty()) {
        body = "{}";
    }

    auto token = bridge.enqueue_command_request(
        std::move(capability),
        std::move(body),
        params.timeout
    );
    auto response = bridge.wait_for_response(token, params.timeout);
    if (!response) {
        set_text(
            res,
            504,
            error_json(
                "Timed out waiting for command",
                504,
                capability_id,
                "command"
            )
        );
        return;
    }
    set_bridge_response(res, *response);
}

void install_routes(httplib::Server& server, Bridge bridge) {
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        set_text(res, 200, detail::c_discovery_json);
    });

    server.Get("/ui", [](const httplib::Request&, httplib::Response& res) {
        set_ui_security_headers(res);
        res.set_redirect("/ui/", 302);
    });

    server.Get("/ui/", [](const httplib::Request&, httplib::Response& res) {
        serve_ui_asset("/ui/", res);
    });

    server.Get(
        "/ui/index.html",
        [](const httplib::Request&, httplib::Response& res) {
            serve_ui_asset("/ui/index.html", res);
        }
    );

    server.Get(
        "/ui/app.css",
        [](const httplib::Request&, httplib::Response& res) {
            serve_ui_asset("/ui/app.css", res);
        }
    );

    server.Get(
        "/ui/app.js",
        [](const httplib::Request&, httplib::Response& res) {
            serve_ui_asset("/ui/app.js", res);
        }
    );

    server.Get(
        "/api/v1/manifest",
        [bridge](const httplib::Request&, httplib::Response& res) mutable {
            set_text(res, 200, bridge.manifest_json());
        }
    );

    server.Get(
        "/api/v1/status",
        [bridge](const httplib::Request&, httplib::Response& res) mutable {
            set_text(res, 200, bridge.status_json());
        }
    );

    server.Get(
        "/api/v1/schemas",
        [bridge](const httplib::Request&, httplib::Response& res) mutable {
            set_text(res, 200, bridge.schema_json());
        }
    );

    server.Get(
        R"(/api/v1/blobs/([^/]+)/stream)",
        [bridge](const httplib::Request& req, httplib::Response& res) mutable {
            const auto capability = req.matches[1].str();
            if (!validate_capability(bridge, capability, "blob", res)) {
                return;
            }

            RequestParams params;
            std::string error_message;
            if (!parse_after_param(req, params, error_message)) {
                set_text(
                    res,
                    400,
                    error_json(error_message, 400, capability, "blob")
                );
                return;
            }

            const auto token = bridge.start_subscription(capability);
            res.set_header("Cache-Control", "no-store");
            res.set_chunked_content_provider(
                "multipart/x-mixed-replace; boundary=fei-frame",
                [bridge, capability, token, frame_index = params.after](
                    std::size_t,
                    httplib::DataSink& sink
                ) mutable {
                    if (!sink.is_writable()) {
                        bridge.stop_subscription(token);
                        return false;
                    }

                    auto frame = bridge.wait_for_blob_after(
                        capability,
                        frame_index,
                        std::chrono::milliseconds {1000}
                    );
                    if (!frame) {
                        return sink.is_writable();
                    }

                    frame_index = frame->version;
                    return write_mjpeg_blob(sink, *frame);
                }
            );
        }
    );

    server.Get(
        R"(/api/v1/blobs/([^/]+))",
        [bridge](const httplib::Request& req, httplib::Response& res) mutable {
            serve_blob_request(bridge, req.matches[1].str(), req, res);
        }
    );

    server.Get(
        R"(/api/v1/snapshots/([^/]+))",
        [bridge](const httplib::Request& req, httplib::Response& res) mutable {
            serve_snapshot_request(bridge, req.matches[1].str(), req, res);
        }
    );

    server.Post(
        R"(/api/v1/commands/([^/]+))",
        [bridge](const httplib::Request& req, httplib::Response& res) mutable {
            serve_command_request(
                bridge,
                req.matches[1].str(),
                req.body,
                req,
                res
            );
        }
    );

    server.Post(
        "/api/v1/shutdown",
        [bridge](const httplib::Request& req, httplib::Response& res) mutable {
            serve_command_request(bridge, "devtools.shutdown", "{}", req, res);
        }
    );
}

} // namespace

Server::Server(Config config, Bridge bridge) :
    m_config(std::move(config)), m_bridge(std::move(bridge)),
    m_server(std::make_unique<httplib::Server>()) {
    install_routes(*m_server, m_bridge);
}

Server::~Server() {
    stop();
}

Server::Server(Server&& other) noexcept :
    m_config(std::move(other.m_config)), m_bridge(std::move(other.m_bridge)),
    m_server(std::move(other.m_server)), m_thread(std::move(other.m_thread)) {}

Server& Server::operator=(Server&& other) noexcept {
    if (this != &other) {
        stop();
        m_config = std::move(other.m_config);
        m_bridge = std::move(other.m_bridge);
        m_server = std::move(other.m_server);
        m_thread = std::move(other.m_thread);
    }
    return *this;
}

void Server::start() {
    if (m_thread.joinable()) {
        return;
    }

    auto* server = m_server.get();
    auto host = m_config.host;
    auto port = m_config.port;
    m_thread = std::thread([server, host = std::move(host), port]() {
        info("DevTools listening at http://{}:{}", host, port);
        if (!server->listen(host, port)) {
            error("DevTools failed to listen at {}:{}", host, port);
        }
    });
}

void Server::stop() {
    if (m_server) {
        m_server->stop();
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

} // namespace fei::devtools
