#include "web_preview/server.hpp"

#include "asset/embed.hpp"
#include "base/log.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

EMBED(index_html, "index.html");

namespace fei {

namespace {

constexpr std::string_view c_mjpeg_boundary {"fei-frame"};
using Json = nlohmann::json;

std::string_view index_html() {
    auto reader = EmbededAssets::get("index.html").reader();
    auto html = reader.as_string_view();
    if (!html.empty() && html.back() == '\0') {
        html.remove_suffix(1);
    }
    return html;
}

Json texture_use_to_json(const WebPreviewRenderGraphTextureUse& use) {
    return Json {
        {"index", use.index},
        {"generation", use.generation},
        {"name", use.name},
        {"access", use.access},
    };
}

Json pass_to_json(const WebPreviewRenderGraphPass& pass) {
    Json reads = Json::array();
    for (const auto& read : pass.reads) {
        reads.push_back(texture_use_to_json(read));
    }

    Json writes = Json::array();
    for (const auto& write : pass.writes) {
        writes.push_back(texture_use_to_json(write));
    }

    return Json {
        {"index", pass.index},
        {"name", pass.name},
        {"active", pass.active},
        {"side_effect", pass.side_effect},
        {"dependencies", pass.dependencies},
        {"reads", std::move(reads)},
        {"writes", std::move(writes)},
    };
}

Json texture_to_json(const WebPreviewRenderGraphTexture& texture) {
    return Json {
        {"index", texture.index},
        {"name", texture.name},
        {"active", texture.active},
        {"imported", texture.imported},
        {"width", texture.width},
        {"height", texture.height},
        {"depth", texture.depth},
        {"mip_level", texture.mip_level},
        {"layer", texture.layer},
        {"format", texture.format},
        {"usage", texture.usage},
        {"type", texture.type},
        {"version_count", texture.version_count},
        {"first_active_use", texture.first_active_use},
        {"last_active_use", texture.last_active_use},
    };
}

Json resource_set_binding_to_json(
    const WebPreviewRenderGraphResourceSetBinding& binding
) {
    Json result {
        {"index", binding.index},
        {"kind", binding.kind},
        {"resource_name", binding.resource_name},
        {"valid", binding.valid},
    };
    if (binding.kind == "texture") {
        result["texture_index"] = binding.texture_index;
        result["texture_generation"] = binding.texture_generation;
    }
    return result;
}

Json resource_set_to_json(
    const WebPreviewRenderGraphResourceSet& resource_set
) {
    Json bindings = Json::array();
    for (const auto& binding : resource_set.bindings) {
        bindings.push_back(resource_set_binding_to_json(binding));
    }

    return Json {
        {"index", resource_set.index},
        {"generation", resource_set.generation},
        {"pass_index", resource_set.pass_index},
        {"name", resource_set.name},
        {"active", resource_set.active},
        {"resolved", resource_set.resolved},
        {"has_layout", resource_set.has_layout},
        {"bindings", std::move(bindings)},
    };
}

Json render_graph_to_json(const WebPreviewDebugStats& debug) {
    const auto& graph = debug.render_graph;
    Json passes = Json::array();
    for (const auto& pass : debug.render_graph_passes) {
        passes.push_back(pass_to_json(pass));
    }

    Json textures = Json::array();
    for (const auto& texture : debug.render_graph_textures) {
        textures.push_back(texture_to_json(texture));
    }

    Json resource_sets = Json::array();
    for (const auto& resource_set : debug.render_graph_resource_sets) {
        resource_sets.push_back(resource_set_to_json(resource_set));
    }

    return Json {
        {"compiled", graph.compiled},
        {"compile_error", graph.compile_error},
        {"total_passes", graph.total_passes},
        {"active_passes", graph.active_passes},
        {"culled_passes", graph.culled_passes},
        {"transient_texture_requests", graph.transient_texture_requests},
        {"transient_texture_hits", graph.transient_texture_hits},
        {"transient_texture_creates", graph.transient_texture_creates},
        {"texture_pool_size", graph.texture_pool_size},
        {"active_order", graph.active_order},
        {"passes", std::move(passes)},
        {"textures", std::move(textures)},
        {"resource_sets", std::move(resource_sets)},
    };
}

Json graphics_cache_to_json(const WebPreviewGraphicsCacheStats& cache) {
    Json sources = Json::array();
    for (const auto& source : cache.resource_set_sources) {
        sources.push_back(
            Json {
                {"name", source.name},
                {"requests", source.requests},
                {"hits", source.hits},
                {"creates", source.creates},
                {"cache_size", source.cache_size},
            }
        );
    }

    return Json {
        {"framebuffer_requests", cache.framebuffer_requests},
        {"framebuffer_hits", cache.framebuffer_hits},
        {"framebuffer_creates", cache.framebuffer_creates},
        {"framebuffer_cache_size", cache.framebuffer_cache_size},
        {"resource_set_requests", cache.resource_set_requests},
        {"resource_set_hits", cache.resource_set_hits},
        {"resource_set_creates", cache.resource_set_creates},
        {"resource_set_cache_size", cache.resource_set_cache_size},
        {"resource_set_sources", std::move(sources)},
    };
}

std::string status_to_json(const WebPreviewStatus& status) {
    return Json {
        {"has_frame", status.has_frame},
        {"width", status.width},
        {"height", status.height},
        {"frame_index", status.frame_index},
        {"frame_age_ms", status.frame_age_ms},
        {"capture_fps", status.capture_fps},
        {"engine_fps", status.engine_fps},
        {"jpeg_bytes", status.jpeg_bytes},
        {"target", status.target},
        {"last_error", status.last_error},
        {"render_graph", render_graph_to_json(status.debug)},
        {"graphics_cache", graphics_cache_to_json(status.debug.graphics_cache)},
    }
        .dump();
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
