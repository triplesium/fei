#include "server.hpp"

#include "artifact_store.hpp"
#include "play_trace_store.hpp"
#include "runtime_protocol/protocol.hpp"
#include "state.hpp"
#include "ui_assets.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <exception>
#include <httplib.h>
#include <memory>
#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fei::agentd {
namespace {

using Json = nlohmann::json;

constexpr std::size_t c_maximum_trace_source_bytes = std::size_t {64} * 1024;
constexpr std::size_t c_maximum_trace_origin_bytes = 128;
constexpr std::size_t c_maximum_event_limit = 500;
constexpr std::chrono::milliseconds c_maximum_event_wait {25000};

struct PlaySpan {
    std::string trace_id;
    std::string span_id;
    std::optional<std::string> parent_span_id;
    std::chrono::steady_clock::time_point started_at;
};

struct PlayHttpError {
    int status {};
    std::string message;
};

struct CapturedFrame {
    uint64 frame {};
    uint32 width {};
    uint32 height {};
    std::string content_type;
    std::vector<std::byte> bytes;
};

std::string make_identifier(std::string_view prefix) {
    static std::atomic<uint64> counter {1};
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        )
            .count();
    return std::string(prefix) + '-' + std::to_string(timestamp) + '-' +
           std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

bool is_valid_identifier(std::string_view value) {
    if (value.empty() || value.size() > 96) {
        return false;
    }
    for (const auto character : value) {
        const auto alpha = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z');
        const auto digit = character >= '0' && character <= '9';
        if (!alpha && !digit && character != '-' && character != '_') {
            return false;
        }
    }
    return true;
}

template<typename Value>
bool parse_integer(std::string_view text, Value& value) {
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc {} && end == text.data() + text.size();
}

void set_json(httplib::Response& response, int status, std::string body) {
    response.status = status;
    response.set_header("Cache-Control", "no-store");
    response.set_content(std::move(body), "application/json");
}

void set_ok(httplib::Response& response) {
    set_json(response, 200, R"({"ok":true})");
}

void set_error(httplib::Response& response, int status, std::string message) {
    set_json(
        response,
        status,
        Json {{"ok", false}, {"message", std::move(message)}}.dump()
    );
}

void serve_ui_asset(std::string_view path, httplib::Response& response) {
    auto asset = detail::find_ui_asset(path);
    if (!asset) {
        set_error(response, 404, "UI asset was not found");
        return;
    }
    response.status = 200;
    response.set_header("Cache-Control", "no-store");
    response.set_header(
        "Content-Security-Policy",
        std::string(detail::c_ui_content_security_policy)
    );
    response.set_header("Referrer-Policy", "no-referrer");
    response.set_header("X-Content-Type-Options", "nosniff");
    response.set_header("X-Frame-Options", "DENY");
    response.set_content(
        asset->content.data(),
        asset->content.size(),
        std::string(asset->content_type)
    );
}

void submit_inspection(
    SupervisorState& state,
    const httplib::Request& request,
    httplib::Response& response
) {
    try {
        const auto body = Json::parse(request.body);
        if (!body.is_object()) {
            set_error(response, 400, "Inspection request must be an object");
            return;
        }
        const auto timeout_ms = body.value("timeout_ms", 5000);
        if (timeout_ms < 1 || timeout_ms > 30000) {
            set_error(
                response,
                400,
                "Inspection timeout_ms must be between 1 and 30000"
            );
            return;
        }
        auto result = state.request_inspection(
            body.at("provider").get<std::string>(),
            body.at("schema").get<std::string>(),
            body.at("payload").dump(),
            std::chrono::milliseconds(timeout_ms)
        );
        if (!result) {
            const auto status =
                result.error() == "Runtime inspection timed out" ? 504 : 409;
            set_error(response, status, std::move(result.error()));
            return;
        }
        auto encoded = runtime_protocol::encode_inspection_response(*result);
        if (!encoded) {
            set_error(response, 500, std::move(encoded.error()));
            return;
        }
        set_json(response, 200, std::move(*encoded));
    } catch (const std::exception& error) {
        set_error(
            response,
            400,
            std::string("Invalid inspection request: ") + error.what()
        );
    }
}

void submit_known_inspection(
    SupervisorState& state,
    httplib::Response& response,
    std::string provider,
    std::string schema,
    std::string payload_json,
    std::chrono::milliseconds timeout
) {
    auto result = state.request_inspection(
        std::move(provider),
        std::move(schema),
        std::move(payload_json),
        timeout
    );
    if (!result) {
        const auto status =
            result.error() == "Runtime inspection timed out" ? 504 : 409;
        set_error(response, status, std::move(result.error()));
        return;
    }
    auto encoded = runtime_protocol::encode_inspection_response(*result);
    if (!encoded) {
        set_error(response, 500, std::move(encoded.error()));
        return;
    }
    set_json(response, 200, std::move(*encoded));
}

template<typename Decode, typename Accept>
void accept_runtime_message(
    const httplib::Request& request,
    httplib::Response& response,
    Decode&& decode,
    Accept&& accept
) {
    auto message = decode(request.body);
    if (!message) {
        set_error(response, 400, std::move(message.error()));
        return;
    }
    auto status = accept(*message);
    if (!status) {
        set_error(response, 409, std::move(status.error()));
        return;
    }
    set_ok(response);
}

} // namespace

class AgentServer::Impl {
  public:
    Impl(SupervisorState& state, uint16 port) :
        m_state(state), m_port(port),
        m_server(std::make_unique<httplib::Server>()) {
        install_routes();
    }

    Status<std::string> start() {
        if (m_thread.joinable()) {
            return {};
        }
        if (!m_server->bind_to_port("127.0.0.1", m_port)) {
            return failure(
                std::string("Failed to bind fei-agentd to 127.0.0.1:") +
                std::to_string(m_port)
            );
        }
        m_thread = std::thread([this]() {
            m_server->listen_after_bind();
        });
        return {};
    }

    void stop() noexcept {
        if (m_server) {
            m_server->stop();
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

  private:
    void append_trace_event(Json event) {
        (void)m_traces.append(std::move(event));
    }

    Result<PlaySpan, std::string> begin_play_span(
        const httplib::Request& request,
        std::string name,
        Json request_data
    ) {
        std::string trace_id;
        std::optional<std::string> parent_span_id;
        if (request.has_header("X-Fei-Trace-Id")) {
            trace_id = request.get_header_value("X-Fei-Trace-Id");
            if (!is_valid_identifier(trace_id)) {
                return failure(std::string("Invalid X-Fei-Trace-Id header"));
            }
        } else {
            trace_id = make_identifier("tr");
        }
        if (request.has_header("X-Fei-Parent-Span-Id")) {
            auto value = request.get_header_value("X-Fei-Parent-Span-Id");
            if (!is_valid_identifier(value)) {
                return failure(
                    std::string("Invalid X-Fei-Parent-Span-Id header")
                );
            }
            parent_span_id = std::move(value);
        }
        if (!request.has_header("X-Fei-Trace-Id") && parent_span_id) {
            return failure(
                std::string("X-Fei-Parent-Span-Id requires X-Fei-Trace-Id")
            );
        }

        Json data {{"request", std::move(request_data)}};
        if (request.has_header("X-Fei-Call-Index")) {
            uint64 call_index {};
            const auto value = request.get_header_value("X-Fei-Call-Index");
            if (!parse_integer(std::string_view(value), call_index) ||
                call_index == 0) {
                return failure(std::string("Invalid X-Fei-Call-Index header"));
            }
            data["call_index"] = call_index;
        }

        PlaySpan span {
            .trace_id = std::move(trace_id),
            .span_id = make_identifier("sp"),
            .parent_span_id = std::move(parent_span_id),
            .started_at = std::chrono::steady_clock::now(),
        };
        append_trace_event(
            Json {
                {"trace_id", span.trace_id},
                {"span_id", span.span_id},
                {"parent_span_id",
                 span.parent_span_id ? Json(*span.parent_span_id) :
                                       Json(nullptr)},
                {"type", "span.started"},
                {"name", name},
                {"data", std::move(data)},
            }
        );
        return span;
    }

    void finish_play_span(
        const PlaySpan& span,
        std::string_view name,
        httplib::Response& response
    ) {
        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - span.started_at
            )
                .count();
        auto body = Json::parse(response.body, nullptr, false);
        if (body.is_discarded()) {
            body = response.body.empty() ? Json(nullptr) : Json(response.body);
        }
        bool ok = response.status >= 200 && response.status < 300;
        if (body.is_object() && body.contains("ok") &&
            body.at("ok").is_boolean()) {
            ok = ok && body.at("ok").get<bool>();
        }
        append_trace_event(
            Json {
                {"trace_id", span.trace_id},
                {"span_id", span.span_id},
                {"parent_span_id",
                 span.parent_span_id ? Json(*span.parent_span_id) :
                                       Json(nullptr)},
                {"type", ok ? "span.completed" : "span.failed"},
                {"name", name},
                {"duration_ms", duration},
                {"data",
                 {
                     {"status", response.status},
                     {"response", std::move(body)},
                 }},
            }
        );
        response.set_header("X-Fei-Trace-Id", span.trace_id);
        response.set_header("X-Fei-Span-Id", span.span_id);
    }

    Result<CapturedFrame, PlayHttpError>
    request_play_frame(const Json& payload) {
        auto result = m_state.request_inspection(
            "play.capture",
            "play.capture.v1",
            payload.dump(),
            std::chrono::seconds(10)
        );
        if (!result) {
            return failure(
                PlayHttpError {
                    .status = result.error() == "Runtime inspection timed out" ?
                                  504 :
                                  409,
                    .message = std::move(result.error()),
                }
            );
        }
        if (!result->ok) {
            return failure(
                PlayHttpError {
                    .status = 409,
                    .message = result->error_message,
                }
            );
        }
        if (result->attachment_content_type != "image/png" ||
            result->attachment.empty()) {
            return failure(
                PlayHttpError {
                    .status = 500,
                    .message =
                        "Runtime capture did not return a PNG attachment",
                }
            );
        }

        CapturedFrame frame;
        try {
            const auto metadata = Json::parse(result->payload_json);
            frame.frame = metadata.at("frame").get<uint64>();
            frame.width = metadata.at("width").get<uint32>();
            frame.height = metadata.at("height").get<uint32>();
            if (metadata.at("format").get<std::string_view>() != "png" ||
                frame.width == 0 || frame.height == 0) {
                throw std::runtime_error("Runtime capture metadata is invalid");
            }
        } catch (const std::exception& error) {
            return failure(
                PlayHttpError {
                    .status = 500,
                    .message =
                        std::string("Invalid runtime capture metadata: ") +
                        error.what(),
                }
            );
        }
        frame.content_type = std::move(result->attachment_content_type);
        frame.bytes = std::move(result->attachment);
        return frame;
    }

    void install_routes() {
        m_server->Get(
            "/",
            [](const httplib::Request&, httplib::Response& response) {
                response.set_redirect("/ui/", 302);
                response.set_header("Cache-Control", "no-store");
            }
        );
        m_server->Get(
            "/ui/",
            [](const httplib::Request&, httplib::Response& response) {
                serve_ui_asset("/ui/", response);
            }
        );
        m_server->Get(
            "/ui/index.html",
            [](const httplib::Request&, httplib::Response& response) {
                serve_ui_asset("/ui/index.html", response);
            }
        );
        m_server->Get(
            "/ui/app.css",
            [](const httplib::Request&, httplib::Response& response) {
                serve_ui_asset("/ui/app.css", response);
            }
        );
        m_server->Get(
            "/ui/app.js",
            [](const httplib::Request&, httplib::Response& response) {
                serve_ui_asset("/ui/app.js", response);
            }
        );
        m_server->Get(
            "/api/v1/status",
            [this](const httplib::Request&, httplib::Response& response) {
                set_json(response, 200, m_state.status_json());
            }
        );
        m_server->Get(
            "/api/v1/project",
            [this](const httplib::Request&, httplib::Response& response) {
                set_json(response, 200, m_state.project_json());
            }
        );
        m_server->Get(
            "/api/v1/capabilities",
            [this](const httplib::Request&, httplib::Response& response) {
                set_json(response, 200, m_state.capabilities_json());
            }
        );
        m_server->Get(
            "/api/v1/play/events",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                uint64 after {};
                std::size_t limit {200};
                int64 timeout_ms {};
                if (request.has_param("after") &&
                    !parse_integer(
                        std::string_view(request.get_param_value("after")),
                        after
                    )) {
                    set_error(response, 400, "Invalid after parameter");
                    return;
                }
                if (request.has_param("limit") &&
                    (!parse_integer(
                         std::string_view(request.get_param_value("limit")),
                         limit
                     ) ||
                     limit == 0 || limit > c_maximum_event_limit)) {
                    set_error(
                        response,
                        400,
                        "Event limit must be between 1 and 500"
                    );
                    return;
                }
                if (request.has_param("timeout_ms") &&
                    (!parse_integer(
                         std::string_view(
                             request.get_param_value("timeout_ms")
                         ),
                         timeout_ms
                     ) ||
                     timeout_ms < 0 ||
                     timeout_ms > c_maximum_event_wait.count())) {
                    set_error(
                        response,
                        400,
                        "Event timeout_ms must be between 0 and 25000"
                    );
                    return;
                }

                const auto batch = timeout_ms == 0 ?
                                       m_traces.read_after(after, limit) :
                                       m_traces.wait_after(
                                           after,
                                           limit,
                                           std::chrono::milliseconds {
                                               timeout_ms,
                                           }
                                       );
                const auto cursor =
                    batch.events.empty() ?
                        after :
                        batch.events.back().at("sequence").get<uint64>();
                set_json(
                    response,
                    200,
                    Json {
                        {"oldest", batch.oldest},
                        {"latest", batch.latest},
                        {"cursor", cursor},
                        {"gap", batch.gap},
                        {"events", batch.events},
                    }
                        .dump()
                );
            }
        );
        m_server->Post(
            "/api/v1/play/traces",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                try {
                    const auto body = Json::parse(request.body);
                    if (!body.is_object() || !body.contains("source") ||
                        !body.at("source").is_string()) {
                        set_error(
                            response,
                            400,
                            "Play trace source must be a string"
                        );
                        return;
                    }
                    const auto source = body.at("source").get<std::string>();
                    const auto origin = body.value("origin", "unknown");
                    if (source.empty() ||
                        source.size() > c_maximum_trace_source_bytes) {
                        set_error(
                            response,
                            400,
                            "Play trace source must be between 1 and 65536 "
                            "bytes"
                        );
                        return;
                    }
                    if (origin.size() > c_maximum_trace_origin_bytes) {
                        set_error(
                            response,
                            400,
                            "Play trace origin exceeds 128 bytes"
                        );
                        return;
                    }
                    const auto trace_id = make_identifier("tr");
                    const auto span_id = make_identifier("sp");
                    append_trace_event(
                        Json {
                            {"trace_id", trace_id},
                            {"span_id", span_id},
                            {"parent_span_id", nullptr},
                            {"type", "play_run.started"},
                            {"name", "play-run"},
                            {"data",
                             {
                                 {"origin", origin},
                                 {"source", source},
                             }},
                        }
                    );
                    set_json(
                        response,
                        201,
                        Json {
                            {"trace_id", trace_id},
                            {"span_id", span_id},
                        }
                            .dump()
                    );
                } catch (const std::exception& error) {
                    set_error(
                        response,
                        400,
                        std::string("Invalid play trace request: ") +
                            error.what()
                    );
                }
            }
        );
        m_server->Post(
            R"(/api/v1/play/traces/([A-Za-z0-9_-]+)/events)",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                try {
                    const auto trace_id = request.matches[1].str();
                    const auto body = Json::parse(request.body);
                    if (!is_valid_identifier(trace_id) || !body.is_object() ||
                        body.value("type", "") != "play_log" ||
                        !body.contains("parent_span_id") ||
                        !body.at("parent_span_id").is_string() ||
                        !is_valid_identifier(
                            body.at("parent_span_id").get<std::string_view>()
                        ) ||
                        !body.contains("data")) {
                        set_error(response, 400, "Invalid play trace event");
                        return;
                    }
                    append_trace_event(
                        Json {
                            {"trace_id", trace_id},
                            {"span_id", make_identifier("ev")},
                            {"parent_span_id", body.at("parent_span_id")},
                            {"type", "play_log"},
                            {"name", "log"},
                            {"data", body.at("data")},
                        }
                    );
                    set_ok(response);
                } catch (const std::exception& error) {
                    set_error(
                        response,
                        400,
                        std::string("Invalid play trace event: ") + error.what()
                    );
                }
            }
        );
        m_server->Post(
            R"(/api/v1/play/traces/([A-Za-z0-9_-]+)/finish)",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                try {
                    const auto trace_id = request.matches[1].str();
                    const auto body = Json::parse(request.body);
                    if (!is_valid_identifier(trace_id) || !body.is_object() ||
                        !body.contains("span_id") ||
                        !body.at("span_id").is_string() ||
                        !is_valid_identifier(
                            body.at("span_id").get<std::string_view>()
                        ) ||
                        !body.contains("report") ||
                        !body.at("report").is_object()) {
                        set_error(response, 400, "Invalid play trace result");
                        return;
                    }
                    const bool ok = body.at("report").value("ok", false);
                    append_trace_event(
                        Json {
                            {"trace_id", trace_id},
                            {"span_id", body.at("span_id")},
                            {"parent_span_id", nullptr},
                            {"type",
                             ok ? "play_run.completed" : "play_run.failed"},
                            {"name", "play-run"},
                            {"data", {{"report", body.at("report")}}},
                        }
                    );
                    set_ok(response);
                } catch (const std::exception& error) {
                    set_error(
                        response,
                        400,
                        std::string("Invalid play trace result: ") +
                            error.what()
                    );
                }
            }
        );
        m_server->Post(
            "/api/v1/restart",
            [this](const httplib::Request&, httplib::Response& response) {
                m_state.request_restart();
                const auto trace_id = make_identifier("tr");
                append_trace_event(
                    Json {
                        {"trace_id", trace_id},
                        {"span_id", make_identifier("sp")},
                        {"parent_span_id", nullptr},
                        {"type", "restart.requested"},
                        {"name", "restart"},
                        {"data", Json::object()},
                    }
                );
                set_ok(response);
            }
        );
        m_server->Get(
            "/api/v1/play/interfaces",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                auto span =
                    begin_play_span(request, "interfaces", Json::object());
                if (!span) {
                    set_error(response, 400, std::move(span.error()));
                    return;
                }
                submit_known_inspection(
                    m_state,
                    response,
                    "play.interfaces",
                    "play.interfaces.v1",
                    "{}",
                    std::chrono::seconds(5)
                );
                finish_play_span(*span, "interfaces", response);
            }
        );
        m_server->Post(
            "/api/v1/play/capture",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                Json payload;
                try {
                    payload = Json::parse(request.body);
                    if (!payload.is_object()) {
                        set_error(
                            response,
                            400,
                            "Playtest capture request must be an object"
                        );
                        return;
                    }
                } catch (const std::exception& error) {
                    set_error(
                        response,
                        400,
                        std::string("Invalid playtest capture request: ") +
                            error.what()
                    );
                    return;
                }

                auto span = begin_play_span(request, "capture", payload);
                if (!span) {
                    set_error(response, 400, std::move(span.error()));
                    return;
                }
                [&]() {
                    auto frame = request_play_frame(payload);
                    if (!frame) {
                        set_error(
                            response,
                            frame.error().status,
                            std::move(frame.error().message)
                        );
                        return;
                    }
                    auto artifact = m_artifacts.store(
                        frame->content_type,
                        std::move(frame->bytes)
                    );
                    if (!artifact) {
                        set_error(response, 500, std::move(artifact.error()));
                        return;
                    }
                    set_json(
                        response,
                        200,
                        Json {
                            {"frame", frame->frame},
                            {"width", frame->width},
                            {"height", frame->height},
                            {"format", "png"},
                            {"content_type", artifact->content_type},
                            {"bytes", artifact->size},
                            {"artifact", "/api/v1/artifacts/" + artifact->id},
                        }
                            .dump()
                    );
                }();
                finish_play_span(*span, "capture", response);
            }
        );
        m_server->Get(
            "/api/v1/play/frame",
            [this](const httplib::Request&, httplib::Response& response) {
                auto frame = request_play_frame(Json::object());
                if (!frame) {
                    set_error(
                        response,
                        frame.error().status,
                        std::move(frame.error().message)
                    );
                    return;
                }
                response.status = 200;
                response.set_header("Cache-Control", "no-store");
                response.set_header(
                    "X-Fei-Frame",
                    std::to_string(frame->frame)
                );
                response.set_header(
                    "X-Fei-Width",
                    std::to_string(frame->width)
                );
                response.set_header(
                    "X-Fei-Height",
                    std::to_string(frame->height)
                );
                response.set_content(
                    reinterpret_cast<const char*>(frame->bytes.data()),
                    frame->bytes.size(),
                    frame->content_type
                );
            }
        );
        m_server->Get(
            R"(/api/v1/artifacts/([A-Za-z0-9-]+))",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                const auto artifact =
                    m_artifacts.find(request.matches[1].str());
                if (!artifact) {
                    set_error(response, 404, "Artifact was not found");
                    return;
                }
                response.status = 200;
                response.set_header("Cache-Control", "no-store");
                response.set_content(
                    reinterpret_cast<const char*>(artifact->data.data()),
                    artifact->data.size(),
                    artifact->metadata.content_type
                );
            }
        );
        m_server->Post(
            "/api/v1/play/observe",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                try {
                    const auto payload = Json::parse(request.body);
                    if (!payload.is_object()) {
                        set_error(
                            response,
                            400,
                            "Playtest observe request must be an object"
                        );
                        return;
                    }
                    auto span = begin_play_span(request, "observe", payload);
                    if (!span) {
                        set_error(response, 400, std::move(span.error()));
                        return;
                    }
                    submit_known_inspection(
                        m_state,
                        response,
                        "play.observe",
                        "play.observe.v1",
                        payload.dump(),
                        std::chrono::seconds(5)
                    );
                    finish_play_span(*span, "observe", response);
                } catch (const std::exception& error) {
                    set_error(
                        response,
                        400,
                        std::string("Invalid playtest observe request: ") +
                            error.what()
                    );
                }
            }
        );
        m_server->Post(
            "/api/v1/play/step",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                try {
                    const auto payload = Json::parse(request.body);
                    if (!payload.is_object()) {
                        set_error(
                            response,
                            400,
                            "Playtest step request must be an object"
                        );
                        return;
                    }
                    auto span = begin_play_span(request, "step", payload);
                    if (!span) {
                        set_error(response, 400, std::move(span.error()));
                        return;
                    }
                    submit_known_inspection(
                        m_state,
                        response,
                        "play.step",
                        "play.step.v1",
                        payload.dump(),
                        std::chrono::seconds(30)
                    );
                    finish_play_span(*span, "step", response);
                } catch (const std::exception& error) {
                    set_error(
                        response,
                        400,
                        std::string("Invalid playtest step request: ") +
                            error.what()
                    );
                }
            }
        );
        m_server->Post(
            "/api/v1/play/reset",
            [this](const httplib::Request&, httplib::Response& response) {
                m_state.request_restart();
                set_ok(response);
            }
        );
        m_server->Post(
            "/api/v1/inspection",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                submit_inspection(m_state, request, response);
            }
        );
        m_server->Get(
            "/api/v1/runtime/inspection/next",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                if (!request.has_header("X-Fei-Runtime-Session")) {
                    set_error(
                        response,
                        400,
                        "Runtime session header is missing"
                    );
                    return;
                }
                auto next = m_state.wait_for_inspection(
                    request.get_header_value("X-Fei-Runtime-Session"),
                    std::chrono::milliseconds(100)
                );
                if (!next) {
                    set_error(response, 409, std::move(next.error()));
                    return;
                }
                if (!*next) {
                    response.status = 204;
                    response.set_header("Cache-Control", "no-store");
                    return;
                }
                auto encoded =
                    runtime_protocol::encode_inspection_request(**next);
                if (!encoded) {
                    set_error(response, 500, std::move(encoded.error()));
                    return;
                }
                set_json(response, 200, std::move(*encoded));
            }
        );
        m_server->Post(
            "/api/v1/runtime/inspection/response",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                accept_runtime_message(
                    request,
                    response,
                    runtime_protocol::decode_inspection_response,
                    [this](
                        const runtime_protocol::InspectionResponse& message
                    ) {
                        return m_state.accept_inspection_response(message);
                    }
                );
            }
        );
        m_server->Post(
            "/api/v1/runtime/hello",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                accept_runtime_message(
                    request,
                    response,
                    runtime_protocol::decode_runtime_hello,
                    [this](const runtime_protocol::RuntimeHello& message) {
                        return m_state.accept_hello(message);
                    }
                );
            }
        );
        m_server->Post(
            "/api/v1/runtime/heartbeat",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                accept_runtime_message(
                    request,
                    response,
                    runtime_protocol::decode_runtime_heartbeat,
                    [this](const runtime_protocol::RuntimeHeartbeat& message) {
                        return m_state.accept_heartbeat(message);
                    }
                );
            }
        );
        m_server->Post(
            "/api/v1/runtime/goodbye",
            [this](
                const httplib::Request& request,
                httplib::Response& response
            ) {
                accept_runtime_message(
                    request,
                    response,
                    runtime_protocol::decode_runtime_goodbye,
                    [this](const runtime_protocol::RuntimeGoodbye& message) {
                        return m_state.accept_goodbye(message);
                    }
                );
            }
        );
    }

    SupervisorState& m_state;
    uint16 m_port;
    std::unique_ptr<httplib::Server> m_server;
    ArtifactStore m_artifacts;
    PlayTraceStore m_traces;
    std::thread m_thread;
};

AgentServer::AgentServer(SupervisorState& state, uint16 port) :
    m_impl(std::make_unique<Impl>(state, port)) {}

AgentServer::~AgentServer() {
    stop();
}

Status<std::string> AgentServer::start() {
    return m_impl->start();
}

void AgentServer::stop() noexcept {
    m_impl->stop();
}

} // namespace fei::agentd
