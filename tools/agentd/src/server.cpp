#include "server.hpp"

#include "artifact_store.hpp"
#include "runtime_protocol/protocol.hpp"
#include "state.hpp"

#include <chrono>
#include <exception>
#include <httplib.h>
#include <memory>
#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace fei::agentd {
namespace {

using Json = nlohmann::json;

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
    void install_routes() {
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
        m_server->Post(
            "/api/v1/restart",
            [this](const httplib::Request&, httplib::Response& response) {
                m_state.request_restart();
                set_ok(response);
            }
        );
        m_server->Get(
            "/api/v1/play/interfaces",
            [this](const httplib::Request&, httplib::Response& response) {
                submit_known_inspection(
                    m_state,
                    response,
                    "play.interfaces",
                    "play.interfaces.v1",
                    "{}",
                    std::chrono::seconds(5)
                );
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

                auto result = m_state.request_inspection(
                    "play.capture",
                    "play.capture.v1",
                    payload.dump(),
                    std::chrono::seconds(10)
                );
                if (!result) {
                    const auto status =
                        result.error() == "Runtime inspection timed out" ? 504 :
                                                                           409;
                    set_error(response, status, std::move(result.error()));
                    return;
                }
                if (!result->ok) {
                    set_error(response, 409, result->error_message);
                    return;
                }
                if (result->attachment_content_type != "image/png" ||
                    result->attachment.empty()) {
                    set_error(
                        response,
                        500,
                        "Runtime capture did not return a PNG attachment"
                    );
                    return;
                }

                uint64 frame {};
                uint32 width {};
                uint32 height {};
                try {
                    const auto capture = Json::parse(result->payload_json);
                    frame = capture.at("frame").get<uint64>();
                    width = capture.at("width").get<uint32>();
                    height = capture.at("height").get<uint32>();
                    if (capture.at("format").get<std::string_view>() != "png" ||
                        width == 0 || height == 0) {
                        throw std::runtime_error(
                            "Runtime capture metadata is invalid"
                        );
                    }
                } catch (const std::exception& error) {
                    set_error(
                        response,
                        500,
                        std::string("Invalid runtime capture metadata: ") +
                            error.what()
                    );
                    return;
                }

                auto artifact = m_artifacts.store(
                    result->attachment_content_type,
                    std::move(result->attachment)
                );
                if (!artifact) {
                    set_error(response, 500, std::move(artifact.error()));
                    return;
                }
                set_json(
                    response,
                    200,
                    Json {
                        {"frame", frame},
                        {"width", width},
                        {"height", height},
                        {"format", "png"},
                        {"content_type", artifact->content_type},
                        {"bytes", artifact->size},
                        {"artifact", "/api/v1/artifacts/" + artifact->id},
                    }
                        .dump()
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
                    submit_known_inspection(
                        m_state,
                        response,
                        "play.observe",
                        "play.observe.v1",
                        payload.dump(),
                        std::chrono::seconds(5)
                    );
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
                    submit_known_inspection(
                        m_state,
                        response,
                        "play.step",
                        "play.step.v1",
                        payload.dump(),
                        std::chrono::seconds(30)
                    );
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
