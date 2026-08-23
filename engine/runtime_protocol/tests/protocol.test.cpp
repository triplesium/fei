#include "runtime_protocol/protocol.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace ets;
using namespace ets::runtime_protocol;

TEST_CASE("Runtime hello messages round trip", "[runtime-protocol][protocol]") {
    const RuntimeHello source {
        .session = "session-1",
        .sequence = 3,
        .process_id = 42,
        .project = "example",
        .project_file = "D:/project/project.yaml",
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
                .request_schema_json = R"({"type":"object"})",
                .response_schema_json = R"({"type":"object"})",
            },
        },
    };

    auto encoded = encode_runtime_hello(source);
    REQUIRE(encoded);
    auto decoded = decode_runtime_hello(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->session == source.session);
    CHECK(decoded->sequence == source.sequence);
    CHECK(decoded->process_id == source.process_id);
    CHECK(decoded->project == source.project);
    CHECK(decoded->project_file == source.project_file);
    CHECK(decoded->build_id == source.build_id);
    REQUIRE(decoded->inspections.size() == 2);
    CHECK(decoded->inspections.at(0).id == "ecs.entity.inspect");
    CHECK(decoded->inspections.at(1).schema == "ecs.query.v1");
    CHECK(decoded->inspections.at(1).description == "Query entities.");
    CHECK(decoded->inspections.at(1).read_only);
    CHECK(decoded->inspections.at(1).cost == "moderate");
    CHECK(
        decoded->inspections.at(1).request_schema_json == R"({"type":"object"})"
    );
}

TEST_CASE(
    "Runtime hello rejects malformed inspection contracts",
    "[runtime-protocol][protocol][inspection]"
) {
    RuntimeHello source {
        .session = "session-contract",
        .sequence = 1,
        .inspections = {
            InspectionCapability {
                .id = "broken.inspect",
                .label = "Broken inspection",
                .description = "A malformed contract.",
                .schema = "broken.inspect.v1",
                .read_only = true,
                .cost = "low",
                .request_schema_json = "not-json",
                .response_schema_json = R"({"type":"object"})",
            },
        },
    };

    auto encoded = encode_runtime_hello(source);
    REQUIRE_FALSE(encoded);
    CHECK(encoded.error().find("not valid JSON") != std::string::npos);
}

TEST_CASE(
    "Runtime heartbeat messages round trip",
    "[runtime-protocol][protocol]"
) {
    const RuntimeHeartbeat source {
        .session = "session-2",
        .sequence = 9,
        .frame = 120,
        .uptime_ms = 3000,
        .lifecycle = RuntimeLifecycle::Running,
    };

    auto encoded = encode_runtime_heartbeat(source);
    REQUIRE(encoded);
    auto decoded = decode_runtime_heartbeat(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->session == source.session);
    CHECK(decoded->sequence == source.sequence);
    CHECK(decoded->frame == source.frame);
    CHECK(decoded->uptime_ms == source.uptime_ms);
    CHECK(decoded->lifecycle == RuntimeLifecycle::Running);
}

TEST_CASE(
    "Runtime messages reject unsupported versions",
    "[runtime-protocol][protocol]"
) {
    const std::string source = R"({
        "version": 99,
        "session": "session-3",
        "sequence": 1,
        "process_id": 12,
        "project": "example",
        "project_file": "project.yaml",
        "build_id": ""
    })";

    auto decoded = decode_runtime_hello(source);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error().find("Unsupported") != std::string::npos);
}

TEST_CASE(
    "Inspection requests preserve structured payloads",
    "[runtime-protocol][inspection]"
) {
    const InspectionRequest source {
        .session = "session-4",
        .request_id = "inspection-12",
        .provider = "ecs.entity.inspect",
        .schema = "ecs.entity.inspect.v1",
        .payload_json = R"({"entity":42})",
    };

    auto encoded = encode_inspection_request(source);
    REQUIRE(encoded);
    auto decoded = decode_inspection_request(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->session == source.session);
    CHECK(decoded->request_id == source.request_id);
    CHECK(decoded->provider == source.provider);
    CHECK(decoded->schema == source.schema);
    CHECK(decoded->payload_json == source.payload_json);
}

TEST_CASE(
    "Inspection responses round trip success and failure",
    "[runtime-protocol][inspection]"
) {
    const InspectionResponse success {
        .session = "session-5",
        .request_id = "inspection-13",
        .ok = true,
        .payload_json = R"({"entity":7,"components":[]})",
    };
    auto encoded_success = encode_inspection_response(success);
    REQUIRE(encoded_success);
    CHECK(encoded_success->find("\"attachment\"") == std::string::npos);
    auto decoded_success = decode_inspection_response(*encoded_success);
    REQUIRE(decoded_success);
    CHECK(decoded_success->ok);
    CHECK(decoded_success->payload_json == R"({"components":[],"entity":7})");

    const InspectionResponse failure_response {
        .session = "session-5",
        .request_id = "inspection-14",
        .ok = false,
        .error_kind = "not_found",
        .error_message = "Entity 99 does not exist",
    };
    auto encoded_failure = encode_inspection_response(failure_response);
    REQUIRE(encoded_failure);
    auto decoded_failure = decode_inspection_response(*encoded_failure);
    REQUIRE(decoded_failure);
    CHECK_FALSE(decoded_failure->ok);
    CHECK(decoded_failure->error_kind == "not_found");
    CHECK(decoded_failure->error_message == failure_response.error_message);
}

TEST_CASE(
    "Inspection responses preserve binary attachments",
    "[runtime-protocol][inspection][attachment]"
) {
    const InspectionResponse source {
        .session = "session-attachment",
        .request_id = "inspection-attachment",
        .ok = true,
        .payload_json = R"({"frame":7,"format":"png"})",
        .attachment_content_type = "image/png",
        .attachment = {
            byte {0x89},
            byte {0x50},
            byte {0x4e},
            byte {0x47},
            byte {0x0d},
        },
    };

    auto encoded = encode_inspection_response(source);
    REQUIRE(encoded);
    auto decoded = decode_inspection_response(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->payload_json == R"({"format":"png","frame":7})");
    CHECK(decoded->attachment_content_type == "image/png");
    CHECK(decoded->attachment == source.attachment);
}
