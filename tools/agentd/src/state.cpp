#include "state.hpp"

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace fei::agentd {
namespace {

using Json = nlohmann::json;

constexpr std::chrono::milliseconds c_stale_after {2000};

} // namespace

SupervisorState::SupervisorState(
    std::string session,
    ProjectDescriptor project
) : m_session(std::move(session)), m_project(std::move(project)) {}

Status<std::string> SupervisorState::validate_session_and_sequence(
    std::string_view session,
    uint64 sequence
) {
    if (session != m_session) {
        return failure(std::string("Runtime session does not match agentd"));
    }
    if (sequence <= m_last_sequence) {
        return failure(
            std::string("Runtime message sequence is not increasing")
        );
    }
    return {};
}

Status<std::string>
SupervisorState::accept_hello(const runtime_protocol::RuntimeHello& hello) {
    std::scoped_lock lock(m_mutex);
    if (hello.session != m_session) {
        return failure(std::string("Runtime session does not match agentd"));
    }
    if (auto status = validate_runtime_project(
            m_project,
            hello.project,
            hello.project_file
        );
        !status) {
        return status;
    }
    if (hello.sequence == 0) {
        return failure(
            std::string("Runtime message sequence must be positive")
        );
    }
    if (m_hello) {
        cancel_pending_inspections("Runtime reconnected");
    }
    m_connected = true;
    m_exit_code.reset();
    m_last_sequence = hello.sequence;
    m_hello = hello;
    m_heartbeat.reset();
    m_last_contact = std::chrono::steady_clock::now();
    return {};
}

Status<std::string> SupervisorState::accept_heartbeat(
    const runtime_protocol::RuntimeHeartbeat& heartbeat
) {
    std::scoped_lock lock(m_mutex);
    if (!m_hello) {
        return failure(std::string("Runtime must send hello before heartbeat"));
    }
    if (auto status = validate_session_and_sequence(
            heartbeat.session,
            heartbeat.sequence
        );
        !status) {
        return status;
    }
    m_connected = true;
    m_last_sequence = heartbeat.sequence;
    m_heartbeat = heartbeat;
    m_last_contact = std::chrono::steady_clock::now();
    return {};
}

Status<std::string> SupervisorState::accept_goodbye(
    const runtime_protocol::RuntimeGoodbye& goodbye
) {
    std::scoped_lock lock(m_mutex);
    if (auto status =
            validate_session_and_sequence(goodbye.session, goodbye.sequence);
        !status) {
        return status;
    }
    m_connected = false;
    cancel_pending_inspections("Runtime disconnected");
    m_last_sequence = goodbye.sequence;
    m_last_contact = std::chrono::steady_clock::now();
    m_inspection_available.notify_all();
    return {};
}

void SupervisorState::mark_process_started(uint64 process_id) {
    std::scoped_lock lock(m_mutex);
    cancel_pending_inspections("Runtime process restarted");
    m_process_running = true;
    m_connected = false;
    m_supervised_process_id = process_id;
    m_last_sequence = 0;
    m_exit_code.reset();
    m_hello.reset();
    m_heartbeat.reset();
    m_inspection_available.notify_all();
}

void SupervisorState::mark_process_exited(int exit_code) {
    std::scoped_lock lock(m_mutex);
    cancel_pending_inspections("Runtime process exited");
    m_process_running = false;
    m_connected = false;
    m_exit_code = exit_code;
    m_inspection_available.notify_all();
}

void SupervisorState::request_restart() {
    std::scoped_lock lock(m_mutex);
    m_restart_requested = true;
}

bool SupervisorState::consume_restart_request() {
    std::scoped_lock lock(m_mutex);
    return std::exchange(m_restart_requested, false);
}

Result<runtime_protocol::InspectionResponse, std::string>
SupervisorState::request_inspection(
    std::string provider,
    std::string schema,
    std::string payload_json,
    std::chrono::milliseconds timeout
) {
    auto completion = std::make_shared<InspectionCompletion>();
    runtime_protocol::InspectionRequest request;
    {
        std::scoped_lock lock(m_mutex);
        if (!m_connected) {
            return failure(std::string("Runtime is not connected"));
        }
        request = runtime_protocol::InspectionRequest {
            .session = m_session,
            .request_id =
                "inspection-" + std::to_string(m_next_inspection_id++),
            .provider = std::move(provider),
            .schema = std::move(schema),
            .payload_json = std::move(payload_json),
        };
        if (auto encoded = runtime_protocol::encode_inspection_request(request);
            !encoded) {
            return failure(std::move(encoded.error()));
        }
        m_inspection_queue.push_back(request.request_id);
        m_pending_inspections.emplace(
            request.request_id,
            PendingInspection {
                .request = request,
                .completion = completion,
            }
        );
    }
    m_inspection_available.notify_one();

    std::unique_lock completion_lock(completion->mutex);
    const auto completed =
        completion->ready.wait_for(completion_lock, timeout, [&completion]() {
            return completion->response.has_value() ||
                   !completion->failure.empty();
        });
    if (!completed) {
        completion_lock.unlock();
        bool removed = false;
        {
            std::scoped_lock lock(m_mutex);
            removed = m_pending_inspections.erase(request.request_id) != 0;
        }
        if (removed) {
            return failure(std::string("Runtime inspection timed out"));
        }
        completion_lock.lock();
        completion->ready.wait(completion_lock, [&completion]() {
            return completion->response.has_value() ||
                   !completion->failure.empty();
        });
    }
    if (completion->response) {
        return std::move(*completion->response);
    }
    return failure(std::move(completion->failure));
}

Result<std::optional<runtime_protocol::InspectionRequest>, std::string>
SupervisorState::wait_for_inspection(
    std::string_view session,
    std::chrono::milliseconds timeout
) {
    std::unique_lock lock(m_mutex);
    if (session != m_session) {
        return failure(std::string("Runtime session does not match agentd"));
    }
    if (!m_connected) {
        return failure(std::string("Runtime is not connected"));
    }

    auto has_request = [this]() {
        while (!m_inspection_queue.empty() &&
               !m_pending_inspections.contains(m_inspection_queue.front())) {
            m_inspection_queue.pop_front();
        }
        return !m_inspection_queue.empty() || !m_connected;
    };
    (void)m_inspection_available.wait_for(lock, timeout, has_request);
    if (!m_connected) {
        return failure(std::string("Runtime is not connected"));
    }
    if (m_inspection_queue.empty()) {
        return std::optional<runtime_protocol::InspectionRequest> {};
    }

    auto request_id = std::move(m_inspection_queue.front());
    m_inspection_queue.pop_front();
    const auto pending = m_pending_inspections.find(request_id);
    if (pending == m_pending_inspections.end()) {
        return std::optional<runtime_protocol::InspectionRequest> {};
    }
    return std::optional<runtime_protocol::InspectionRequest> {
        pending->second.request,
    };
}

Status<std::string> SupervisorState::accept_inspection_response(
    const runtime_protocol::InspectionResponse& response
) {
    std::shared_ptr<InspectionCompletion> completion;
    {
        std::scoped_lock lock(m_mutex);
        if (response.session != m_session) {
            return failure(
                std::string("Runtime session does not match agentd")
            );
        }
        const auto pending = m_pending_inspections.find(response.request_id);
        if (pending == m_pending_inspections.end()) {
            return failure(
                std::string("Inspection request is unknown or expired")
            );
        }
        completion = pending->second.completion;
        m_pending_inspections.erase(pending);
        m_last_contact = std::chrono::steady_clock::now();
    }
    {
        std::scoped_lock lock(completion->mutex);
        completion->response = response;
    }
    completion->ready.notify_all();
    return {};
}

void SupervisorState::cancel_pending_inspections(std::string_view reason) {
    for (auto& [request_id, pending] : m_pending_inspections) {
        (void)request_id;
        {
            std::scoped_lock lock(pending.completion->mutex);
            pending.completion->failure = reason;
        }
        pending.completion->ready.notify_all();
    }
    m_pending_inspections.clear();
    m_inspection_queue.clear();
}

std::string SupervisorState::status_json() const {
    std::scoped_lock lock(m_mutex);
    const auto now = std::chrono::steady_clock::now();
    const bool stale = m_connected && now - m_last_contact > c_stale_after;

    std::string connection = "waiting";
    if (m_exit_code) {
        connection = "exited";
    } else if (stale) {
        connection = "stale";
    } else if (m_connected) {
        connection = "connected";
    } else if (m_hello) {
        connection = "disconnected";
    }

    Json runtime {
        {"connection", connection},
        {"process_running", m_process_running},
    };
    if (m_supervised_process_id != 0) {
        runtime["supervised_process_id"] = m_supervised_process_id;
    } else {
        runtime["supervised_process_id"] = nullptr;
    }
    if (m_exit_code) {
        runtime["exit_code"] = *m_exit_code;
    } else {
        runtime["exit_code"] = nullptr;
    }
    if (m_hello) {
        runtime["reported_process_id"] = m_hello->process_id;
        runtime["project"] = m_hello->project;
        runtime["project_file"] = m_hello->project_file;
        runtime["build_id"] = m_hello->build_id;
    } else {
        runtime["reported_process_id"] = nullptr;
        runtime["project"] = nullptr;
        runtime["project_file"] = nullptr;
        runtime["build_id"] = nullptr;
    }
    if (m_heartbeat) {
        runtime["frame"] = m_heartbeat->frame;
        runtime["uptime_ms"] = m_heartbeat->uptime_ms;
        runtime["lifecycle"] =
            runtime_protocol::runtime_lifecycle_name(m_heartbeat->lifecycle);
        runtime["last_sequence"] = m_heartbeat->sequence;
    } else {
        runtime["frame"] = nullptr;
        runtime["uptime_ms"] = nullptr;
        if (m_hello) {
            runtime["lifecycle"] = "starting";
        } else {
            runtime["lifecycle"] = nullptr;
        }
        runtime["last_sequence"] = m_last_sequence;
    }
    if (m_exit_code) {
        runtime["lifecycle"] = "stopped";
    }

    return Json {
        {"version", runtime_protocol::protocol_version},
        {"session", m_session},
        {"project",
         Json {
             {"name", m_project.name},
             {"project_file", m_project.project_file.generic_string()},
             {"project_root", m_project.project_root.generic_string()},
             {"asset_root", m_project.asset_root.generic_string()},
             {"cache_root", m_project.cache_root.generic_string()},
         }},
        {"runtime", std::move(runtime)},
    }
        .dump();
}

std::string SupervisorState::project_json() const {
    std::scoped_lock lock(m_mutex);
    return Json {
        {"version", runtime_protocol::protocol_version},
        {"project",
         Json {
             {"name", m_project.name},
             {"project_file", m_project.project_file.generic_string()},
             {"project_root", m_project.project_root.generic_string()},
             {"asset_root", m_project.asset_root.generic_string()},
             {"cache_root", m_project.cache_root.generic_string()},
         }},
    }
        .dump();
}

std::string SupervisorState::capabilities_json() const {
    std::scoped_lock lock(m_mutex);
    Json inspections = Json::array();
    if (m_hello) {
        for (const auto& capability : m_hello->inspections) {
            auto request_schema =
                Json::parse(capability.request_schema_json, nullptr, false);
            auto response_schema =
                Json::parse(capability.response_schema_json, nullptr, false);
            if (request_schema.is_discarded()) {
                request_schema = Json::object();
            }
            if (response_schema.is_discarded()) {
                response_schema = Json::object();
            }
            inspections.push_back(
                Json {
                    {"id", capability.id},
                    {"label", capability.label},
                    {"description", capability.description},
                    {"schema", capability.schema},
                    {"read_only", capability.read_only},
                    {"cost", capability.cost},
                    {"request_schema", std::move(request_schema)},
                    {"response_schema", std::move(response_schema)},
                }
            );
        }
    }
    return Json {
        {"version", runtime_protocol::protocol_version},
        {"session", m_session},
        {"connected", m_connected},
        {"inspections", std::move(inspections)},
    }
        .dump();
}

} // namespace fei::agentd
