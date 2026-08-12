#include "runtime_protocol/probe.hpp"

#include "app/app.hpp"
#include "base/env.hpp"
#include "base/log.hpp"
#include "ecs/system_params.hpp"
#include "runtime_protocol/protocol.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <httplib.h>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#    include <Windows.h> // IWYU pragma: keep
#else
#    include <unistd.h>
#endif

namespace fei::runtime_protocol {
namespace {

constexpr std::string_view c_loopback_host {"127.0.0.1"};
constexpr std::chrono::milliseconds c_reconnect_interval {500};
constexpr std::chrono::milliseconds c_inspection_wait_interval {5};

uint64 current_process_id() {
#if defined(_WIN32)
    return static_cast<uint64>(GetCurrentProcessId());
#else
    return static_cast<uint64>(getpid());
#endif
}

bool post_message(
    httplib::Client& client,
    std::string_view path,
    std::string body
) {
    auto response = client.Post(std::string(path), body, "application/json");
    return response && response->status >= 200 && response->status < 300;
}

void advance_runtime_probe(WorldRef world, ResRW<RuntimeProbe> probe) {
    probe->on_frame(*world);
}

} // namespace

class RuntimeProbe::Impl {
  public:
    explicit Impl(RuntimeProbeConfig config) : m_config(std::move(config)) {
        const auto port = read_environment_variable<uint16>("FEI_AGENTD_PORT");
        const auto session = read_environment_variable("FEI_RUNTIME_SESSION");
        if (!port && !session) {
            return;
        }
        if (!port || !session || session->empty()) {
            std::scoped_lock lock(m_status_mutex);
            m_last_error =
                "FEI_AGENTD_PORT and FEI_RUNTIME_SESSION must be set together";
            warn("Runtime probe disabled: {}", m_last_error);
            return;
        }

        if (const auto build_id =
                read_environment_variable("FEI_RUNTIME_BUILD_ID")) {
            m_config.build_id = *build_id;
        }
        m_port = *port;
        m_session = *session;
        m_enabled = true;
        m_started_at = std::chrono::steady_clock::now();
        m_worker = std::thread([this]() {
            run();
        });
    }

    ~Impl() { stop(); }

    void on_frame(World& world) {
        m_frame.fetch_add(1, std::memory_order_relaxed);
        m_lifecycle.store(RuntimeLifecycle::Running, std::memory_order_relaxed);

        std::deque<InspectionRequest> requests;
        {
            std::scoped_lock lock(m_inspection_mutex);
            requests.swap(m_inspection_requests);
        }
        for (const auto& request : requests) {
            InspectionResponse response {
                .session = request.session,
                .request_id = request.request_id,
            };
            if (!m_config.inspection_handler) {
                response.error_kind = "unsupported";
                response.error_message =
                    "Runtime has no inspection handler installed";
            } else {
                try {
                    auto result = m_config.inspection_handler(world, request);
                    if (result) {
                        response.ok = true;
                        response.payload_json = std::move(*result);
                    } else {
                        response.error_kind = std::move(result.error().kind);
                        response.error_message =
                            std::move(result.error().message);
                    }
                } catch (const std::exception& error) {
                    response.error_kind = "internal";
                    response.error_message =
                        std::string("Inspection handler failed: ") +
                        error.what();
                } catch (...) {
                    response.error_kind = "internal";
                    response.error_message =
                        "Inspection handler failed with an unknown exception";
                }
            }
            {
                std::scoped_lock lock(m_inspection_mutex);
                m_inspection_responses.push_back(std::move(response));
            }
            m_wake.notify_all();
        }
    }

    void stop() noexcept {
        if (!m_enabled || !m_running.exchange(false)) {
            return;
        }
        m_lifecycle.store(
            RuntimeLifecycle::Stopping,
            std::memory_order_relaxed
        );
        m_wake.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    [[nodiscard]] RuntimeProbeStatus status() const {
        std::scoped_lock lock(m_status_mutex);
        return RuntimeProbeStatus {
            .enabled = m_enabled,
            .connected = m_connected.load(std::memory_order_relaxed),
            .frame = m_frame.load(std::memory_order_relaxed),
            .last_error = m_last_error,
        };
    }

  private:
    void set_connection(bool connected, std::string error = {}) {
        m_connected.store(connected, std::memory_order_relaxed);
        std::scoped_lock lock(m_status_mutex);
        m_last_error = std::move(error);
    }

    bool wait_for(std::chrono::milliseconds duration) {
        std::unique_lock lock(m_wait_mutex);
        m_wake.wait_for(lock, duration, [this]() {
            return !m_running.load(std::memory_order_relaxed);
        });
        return m_running.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64 next_sequence() {
        return m_sequence.fetch_add(1, std::memory_order_relaxed);
    }

    bool send_hello(httplib::Client& client) {
        auto body = encode_runtime_hello(
            RuntimeHello {
                .session = m_session,
                .sequence = next_sequence(),
                .process_id = current_process_id(),
                .project = m_config.project,
                .project_file = m_config.project_file,
                .build_id = m_config.build_id,
                .inspections = m_config.inspections,
            }
        );
        return body && post_message(client, "/api/v1/runtime/hello", *body);
    }

    bool send_heartbeat(httplib::Client& client) {
        const auto uptime =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_started_at
            );
        auto body = encode_runtime_heartbeat(
            RuntimeHeartbeat {
                .session = m_session,
                .sequence = next_sequence(),
                .frame = m_frame.load(std::memory_order_relaxed),
                .uptime_ms = static_cast<uint64>(uptime.count()),
                .lifecycle = m_lifecycle.load(std::memory_order_relaxed),
            }
        );
        return body && post_message(client, "/api/v1/runtime/heartbeat", *body);
    }

    bool flush_inspection_responses(httplib::Client& client) {
        while (true) {
            InspectionResponse response;
            {
                std::scoped_lock lock(m_inspection_mutex);
                if (m_inspection_responses.empty()) {
                    return true;
                }
                response = std::move(m_inspection_responses.front());
                m_inspection_responses.pop_front();
            }

            auto body = encode_inspection_response(response);
            if (!body) {
                response.ok = false;
                response.payload_json = "null";
                response.error_kind = "internal";
                response.error_message =
                    "Runtime produced an invalid inspection response: " +
                    body.error();
                body = encode_inspection_response(response);
            }
            if (!body) {
                std::scoped_lock lock(m_inspection_mutex);
                m_inspection_responses.push_front(std::move(response));
                return false;
            }
            auto posted = client.Post(
                "/api/v1/runtime/inspection/response",
                *body,
                "application/json"
            );
            if (!posted) {
                std::scoped_lock lock(m_inspection_mutex);
                m_inspection_responses.push_front(std::move(response));
                return false;
            }
            if (posted->status == 409) {
                warn(
                    "Dropping expired inspection response {}",
                    response.request_id
                );
                m_inspection_in_flight.store(false, std::memory_order_relaxed);
                continue;
            }
            if (posted->status < 200 || posted->status >= 300) {
                std::scoped_lock lock(m_inspection_mutex);
                m_inspection_responses.push_front(std::move(response));
                return false;
            }
            m_inspection_in_flight.store(false, std::memory_order_relaxed);
        }
    }

    bool poll_inspection_request(httplib::Client& client) {
        const httplib::Headers headers {
            {"X-Fei-Runtime-Session", m_session},
        };
        auto response = client.Get("/api/v1/runtime/inspection/next", headers);
        if (!response) {
            return false;
        }
        if (response->status == 204) {
            return true;
        }
        if (response->status < 200 || response->status >= 300) {
            return false;
        }
        auto request = decode_inspection_request(response->body);
        if (!request) {
            warn(
                "Rejected invalid inspection request from fei-agentd: {}",
                request.error()
            );
            return false;
        }
        {
            std::scoped_lock lock(m_inspection_mutex);
            m_inspection_requests.push_back(std::move(*request));
        }
        m_inspection_in_flight.store(true, std::memory_order_relaxed);
        return true;
    }

    void send_goodbye(httplib::Client& client) {
        auto body = encode_runtime_goodbye(
            RuntimeGoodbye {
                .session = m_session,
                .sequence = next_sequence(),
                .reason = "runtime shutdown",
            }
        );
        if (body) {
            (void)post_message(client, "/api/v1/runtime/goodbye", *body);
        }
    }

    void run() {
        httplib::Client client(std::string(c_loopback_host), m_port);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 500000);
        client.set_write_timeout(0, 500000);

        while (m_running.load(std::memory_order_relaxed)) {
            if (!send_hello(client)) {
                set_connection(false, "Failed to connect to fei-agentd");
                if (!wait_for(c_reconnect_interval)) {
                    break;
                }
                continue;
            }

            set_connection(true);
            info(
                "Runtime probe connected to fei-agentd on {}:{}",
                c_loopback_host,
                m_port
            );
            auto next_heartbeat =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(m_config.heartbeat_interval_ms);
            while (m_running.load(std::memory_order_relaxed)) {
                if (!flush_inspection_responses(client)) {
                    set_connection(false, "Runtime inspection response failed");
                    break;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_heartbeat) {
                    if (!send_heartbeat(client)) {
                        set_connection(false, "Runtime heartbeat failed");
                        break;
                    }
                    next_heartbeat = now + std::chrono::milliseconds(
                                               m_config.heartbeat_interval_ms
                                           );
                }
                if (m_inspection_in_flight.load(std::memory_order_relaxed)) {
                    if (!wait_for(c_inspection_wait_interval)) {
                        break;
                    }
                    continue;
                }
                if (!poll_inspection_request(client)) {
                    set_connection(false, "Runtime inspection poll failed");
                    break;
                }
            }
        }

        if (m_connected.load(std::memory_order_relaxed)) {
            send_goodbye(client);
        }
        set_connection(false);
    }

    RuntimeProbeConfig m_config;
    uint16 m_port {0};
    std::string m_session;
    bool m_enabled {false};
    std::atomic<bool> m_running {true};
    std::atomic<bool> m_connected {false};
    std::atomic<uint64> m_sequence {1};
    std::atomic<uint64> m_frame {0};
    std::atomic<bool> m_inspection_in_flight {false};
    std::atomic<RuntimeLifecycle> m_lifecycle {RuntimeLifecycle::Starting};
    std::chrono::steady_clock::time_point m_started_at;
    mutable std::mutex m_status_mutex;
    std::string m_last_error;
    std::mutex m_wait_mutex;
    std::condition_variable m_wake;
    std::mutex m_inspection_mutex;
    std::deque<InspectionRequest> m_inspection_requests;
    std::deque<InspectionResponse> m_inspection_responses;
    std::thread m_worker;
};

RuntimeProbe::RuntimeProbe(RuntimeProbeConfig config) :
    m_impl(std::make_unique<Impl>(std::move(config))) {}

RuntimeProbe::RuntimeProbe(RuntimeProbe&&) noexcept = default;

RuntimeProbe& RuntimeProbe::operator=(RuntimeProbe&&) noexcept = default;

RuntimeProbe::~RuntimeProbe() = default;

void RuntimeProbe::on_frame(World& world) {
    m_impl->on_frame(world);
}

void RuntimeProbe::stop() noexcept {
    m_impl->stop();
}

RuntimeProbeStatus RuntimeProbe::status() const {
    return m_impl->status();
}

void RuntimeProbePlugin::setup(App& app) {
    app.add_resource(RuntimeProbe(std::move(m_config)))
        .add_systems(Last, advance_runtime_probe);
}

void RuntimeProbePlugin::cleanup(App& app) noexcept {
    app.resource<RuntimeProbe>().stop();
}

} // namespace fei::runtime_protocol
