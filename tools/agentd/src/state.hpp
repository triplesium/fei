#pragma once

#include "base/result.hpp"
#include "base/types.hpp"
#include "project_descriptor.hpp"
#include "runtime_protocol/protocol.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ets::agentd {

class SupervisorState {
  public:
    SupervisorState(std::string session, ProjectDescriptor project);

    [[nodiscard]] const std::string& session() const { return m_session; }

    Status<std::string>
    accept_hello(const runtime_protocol::RuntimeHello& hello);
    Status<std::string>
    accept_heartbeat(const runtime_protocol::RuntimeHeartbeat& heartbeat);
    Status<std::string>
    accept_goodbye(const runtime_protocol::RuntimeGoodbye& goodbye);

    void mark_process_started(uint64 process_id);
    void mark_process_exited(int exit_code);

    void request_restart();
    [[nodiscard]] bool consume_restart_request();

    [[nodiscard]] Result<runtime_protocol::InspectionResponse, std::string>
    request_inspection(
        std::string provider,
        std::string schema,
        std::string payload_json,
        std::chrono::milliseconds timeout
    );
    [[nodiscard]] Result<
        std::optional<runtime_protocol::InspectionRequest>,
        std::string>
    wait_for_inspection(
        std::string_view session,
        std::chrono::milliseconds timeout
    );
    Status<std::string> accept_inspection_response(
        const runtime_protocol::InspectionResponse& response
    );

    [[nodiscard]] std::string status_json() const;
    [[nodiscard]] std::string project_json() const;
    [[nodiscard]] std::string capabilities_json() const;

  private:
    Status<std::string>
    validate_session_and_sequence(std::string_view session, uint64 sequence);
    void cancel_pending_inspections(std::string_view reason);

    struct InspectionCompletion {
        std::mutex mutex;
        std::condition_variable ready;
        std::optional<runtime_protocol::InspectionResponse> response;
        std::string failure;
    };

    struct PendingInspection {
        runtime_protocol::InspectionRequest request;
        std::shared_ptr<InspectionCompletion> completion;
    };

    std::string m_session;
    ProjectDescriptor m_project;
    mutable std::mutex m_mutex;
    bool m_connected {false};
    bool m_process_running {false};
    bool m_restart_requested {false};
    uint64 m_supervised_process_id {0};
    uint64 m_last_sequence {0};
    std::optional<int> m_exit_code;
    std::optional<runtime_protocol::RuntimeHello> m_hello;
    std::optional<runtime_protocol::RuntimeHeartbeat> m_heartbeat;
    std::chrono::steady_clock::time_point m_last_contact;
    uint64 m_next_inspection_id {1};
    std::condition_variable m_inspection_available;
    std::deque<std::string> m_inspection_queue;
    std::unordered_map<std::string, PendingInspection> m_pending_inspections;
};

} // namespace ets::agentd
