#include "state.hpp"

#include "runtime_protocol/protocol.hpp"

#include <catch2/catch_test_macros.hpp>
#include <future>
#include <nlohmann/json.hpp>
#include <string>

using namespace fei;
using namespace fei::agentd;
using namespace fei::runtime_protocol;

namespace {

ProjectDescriptor test_project() {
    return ProjectDescriptor {
        .name = "example",
        .project_file = "project.yaml",
        .project_root = ".",
        .asset_root = "assets",
        .cache_root = ".fei",
    };
}

RuntimeHello test_hello(std::string session, uint64 sequence) {
    return RuntimeHello {
        .session = std::move(session),
        .sequence = sequence,
        .project = "example",
        .project_file = "project.yaml",
    };
}

} // namespace

TEST_CASE("Supervisor accepts runtime hello and heartbeat", "[agentd][state]") {
    SupervisorState state("session-1", test_project());
    state.mark_process_started(42);

    REQUIRE(state.accept_hello(
        RuntimeHello {
            .session = "session-1",
            .sequence = 1,
            .process_id = 42,
            .project = "example",
            .project_file = "project.yaml",
            .build_id = "abc123",
            .inspections = {
                InspectionCapability {
                    .id = "ecs.entity.inspect",
                    .label = "Inspect ECS Entity",
                    .description = "Inspect one entity.",
                    .schema = "ecs.entity.inspect.v1",
                    .read_only = true,
                    .cost = "low",
                    .request_schema_json = R"({"type":"object"})",
                    .response_schema_json = R"({"type":"object"})",
                },
                InspectionCapability {
                    .id = "ecs.query",
                    .label = "Query ECS Entities",
                    .description = "Query entities.",
                    .schema = "ecs.query.v1",
                    .read_only = true,
                    .cost = "moderate",
                    .request_schema_json =
                        R"({"type":"object","properties":{"limit":{"maximum":200}}})",
                    .response_schema_json = R"({"type":"object"})",
                },
            },
        }
    ));
    REQUIRE(state.accept_heartbeat(
        RuntimeHeartbeat {
            .session = "session-1",
            .sequence = 2,
            .frame = 99,
            .uptime_ms = 2000,
            .lifecycle = RuntimeLifecycle::Running,
        }
    ));

    const auto status = nlohmann::json::parse(state.status_json());
    CHECK(status.at("runtime").at("connection") == "connected");
    CHECK(status.at("runtime").at("process_running") == true);
    CHECK(status.at("runtime").at("frame") == 99);
    CHECK(status.at("runtime").at("project") == "example");

    const auto capabilities = nlohmann::json::parse(state.capabilities_json());
    CHECK(capabilities.at("connected") == true);
    CHECK(
        capabilities.at("play").at("interfaces") == "/api/v1/play/interfaces"
    );
    CHECK(capabilities.at("play").at("capture") == "/api/v1/play/capture");
    CHECK(capabilities.at("play").at("frame") == "/api/v1/play/frame");
    CHECK(capabilities.at("play").at("observe") == "/api/v1/play/observe");
    CHECK(capabilities.at("play").at("step") == "/api/v1/play/step");
    CHECK(capabilities.at("play").at("events") == "/api/v1/play/events");
    CHECK(capabilities.at("play").at("ui") == "/ui/");
    REQUIRE(capabilities.at("inspections").size() == 2);
    CHECK(
        capabilities.at("inspections").at(0).at("id") == "ecs.entity.inspect"
    );
    CHECK(capabilities.at("inspections").at(1).at("schema") == "ecs.query.v1");
    CHECK(
        capabilities.at("inspections").at(1).at("description") ==
        "Query entities."
    );
    CHECK(capabilities.at("inspections").at(1).at("read_only") == true);
    CHECK(capabilities.at("inspections").at(1).at("cost") == "moderate");
    CHECK(
        capabilities.at("inspections")
            .at(1)
            .at("request_schema")
            .at("properties")
            .at("limit")
            .at("maximum") == 200
    );
}

TEST_CASE(
    "Supervisor exposes its bound project while runtime is offline",
    "[agentd][state][project]"
) {
    SupervisorState state("session-1", test_project());

    const auto project = nlohmann::json::parse(state.project_json());
    CHECK(project.at("project").at("name") == "example");
    CHECK(project.at("project").at("project_file") == "project.yaml");

    const auto status = nlohmann::json::parse(state.status_json());
    CHECK(status.at("project").at("name") == "example");
    CHECK(status.at("runtime").at("connection") == "waiting");
}

TEST_CASE(
    "Supervisor only accepts the runtime for its bound project",
    "[agentd][state][project]"
) {
    SupervisorState state("session-1", test_project());

    auto wrong_name = test_hello("session-1", 1);
    wrong_name.project = "other";
    auto result = state.accept_hello(wrong_name);
    REQUIRE_FALSE(result);
    CHECK(
        result.error() == "Runtime project name does not match agentd project"
    );

    auto wrong_file = test_hello("session-1", 1);
    wrong_file.project_file = "other-project.yaml";
    result = state.accept_hello(wrong_file);
    REQUIRE_FALSE(result);
    CHECK(
        result.error() == "Runtime project file does not match agentd project"
    );

    auto equivalent_file = test_hello("session-1", 1);
    equivalent_file.project_file = "./project.yaml";
    CHECK(state.accept_hello(equivalent_file));
}

TEST_CASE(
    "Supervisor rejects foreign and out-of-order runtime messages",
    "[agentd][state]"
) {
    SupervisorState state("session-1", test_project());
    REQUIRE_FALSE(state.accept_hello(
        RuntimeHello {
            .session = "other-session",
            .sequence = 1,
        }
    ));
    REQUIRE(state.accept_hello(test_hello("session-1", 3)));
    REQUIRE_FALSE(state.accept_heartbeat(
        RuntimeHeartbeat {
            .session = "session-1",
            .sequence = 2,
        }
    ));
}

TEST_CASE("Supervisor records process exits", "[agentd][state]") {
    SupervisorState state("session-1", test_project());
    state.mark_process_started(42);
    REQUIRE(state.accept_hello(
        RuntimeHello {
            .session = "session-1",
            .sequence = 1,
            .process_id = 42,
            .project = "example",
            .project_file = "project.yaml",
        }
    ));

    state.mark_process_exited(7);

    const auto status = nlohmann::json::parse(state.status_json());
    CHECK(status.at("runtime").at("connection") == "exited");
    CHECK(status.at("runtime").at("process_running") == false);
    CHECK(status.at("runtime").at("exit_code") == 7);
    CHECK(status.at("runtime").at("lifecycle") == "stopped");
}

TEST_CASE("Supervisor restart requests are consumed once", "[agentd][state]") {
    SupervisorState state("session-1", test_project());
    state.request_restart();
    CHECK(state.consume_restart_request());
    CHECK_FALSE(state.consume_restart_request());
}

TEST_CASE(
    "Supervisor correlates inspection requests and responses",
    "[agentd][state][inspection]"
) {
    using namespace std::chrono_literals;

    SupervisorState state("session-1", test_project());
    REQUIRE(state.accept_hello(test_hello("session-1", 1)));

    auto client = std::async(std::launch::async, [&state]() {
        return state.request_inspection(
            "ecs.entity.inspect",
            "ecs.entity.inspect.v1",
            R"({"entity":0})",
            500ms
        );
    });

    auto next = state.wait_for_inspection("session-1", 500ms);
    REQUIRE(next);
    REQUIRE(*next);
    const auto request = next->value_or(InspectionRequest {});
    CHECK(request.provider == "ecs.entity.inspect");
    CHECK(request.payload_json == R"({"entity":0})");

    REQUIRE(state.accept_inspection_response(
        InspectionResponse {
            .session = "session-1",
            .request_id = request.request_id,
            .ok = true,
            .payload_json = R"({"entity":0,"components":[]})",
        }
    ));
    auto response = client.get();
    REQUIRE(response);
    CHECK(response->ok);
    CHECK(response->request_id == request.request_id);
}

TEST_CASE(
    "Supervisor expires inspection requests",
    "[agentd][state][inspection]"
) {
    using namespace std::chrono_literals;

    SupervisorState state("session-1", test_project());
    REQUIRE(state.accept_hello(test_hello("session-1", 1)));

    auto response = state.request_inspection(
        "ecs.entity.inspect",
        "ecs.entity.inspect.v1",
        R"({"entity":0})",
        1ms
    );
    REQUIRE_FALSE(response);
    CHECK(response.error() == "Runtime inspection timed out");

    auto next = state.wait_for_inspection("session-1", 1ms);
    REQUIRE(next);
    CHECK_FALSE(*next);
}

TEST_CASE(
    "Supervisor rejects inspection responses that arrive after timeout",
    "[agentd][state][inspection]"
) {
    using namespace std::chrono_literals;

    SupervisorState state("session-1", test_project());
    REQUIRE(state.accept_hello(test_hello("session-1", 1)));

    auto client = std::async(std::launch::async, [&state]() {
        return state.request_inspection(
            "ecs.entity.inspect",
            "ecs.entity.inspect.v1",
            R"({"entity":0})",
            5ms
        );
    });
    auto next = state.wait_for_inspection("session-1", 100ms);
    REQUIRE(next);
    REQUIRE(*next);
    const auto request_id = next->value_or(InspectionRequest {}).request_id;

    auto result = client.get();
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(state.accept_inspection_response(
        InspectionResponse {
            .session = "session-1",
            .request_id = request_id,
            .ok = true,
            .payload_json = R"({"entity":0})",
        }
    ));
}
