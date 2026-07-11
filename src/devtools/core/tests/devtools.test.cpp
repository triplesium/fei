#include "devtools/bridge.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::devtools;

TEST_CASE("Bridge queues requests and completes responses", "[devtools]") {
    Bridge bridge;

    auto token = bridge.enqueue_blob_request(
        "rendering.frame",
        0,
        true,
        std::chrono::milliseconds {100}
    );
    auto pending = bridge.take_pending_requests();
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].token == token);
    REQUIRE(pending[0].capability == "rendering.frame");
    REQUIRE(pending[0].kind == RequestKind::Blob);
    REQUIRE(pending[0].fresh);

    bridge.publish_blob(
        BlobResponse {
            .token = token,
            .capability = "rendering.frame",
            .bytes = {byte {0x01}, byte {0x02}},
            .mime = "image/jpeg",
            .version = 7,
        }
    );

    auto response =
        bridge.wait_for_response(token, std::chrono::milliseconds {1});
    REQUIRE(response);
    REQUIRE(response->status == 200);
    REQUIRE(response->binary);
    REQUIRE(response->content_type == "image/jpeg");
    REQUIRE(response->bytes == std::vector<byte> {byte {0x01}, byte {0x02}});
    REQUIRE(response->headers.at("X-DevTools-Capability") == "rendering.frame");
    REQUIRE(response->headers.at("X-DevTools-Version") == "7");
    REQUIRE(
        response->headers.at("X-DevTools-Metadata").find("\"version\":7") !=
        std::string::npos
    );

    auto cached = bridge.cached_blob("rendering.frame", 6, false);
    REQUIRE(cached);
    REQUIRE(cached->version == 7);
    REQUIRE_FALSE(bridge.cached_blob("rendering.frame", 7, false));
    REQUIRE_FALSE(bridge.cached_blob("rendering.frame", 0, true));
}

TEST_CASE("Bridge reports timeout and errors", "[devtools]") {
    Bridge bridge;

    auto token = bridge.enqueue_command_request(
        "input.clear",
        "{}",
        std::chrono::milliseconds {1}
    );
    REQUIRE_FALSE(
        bridge.wait_for_response(token, std::chrono::milliseconds {1})
    );

    auto error_token = bridge.enqueue_snapshot_request(
        "missing.snapshot",
        0,
        true,
        std::chrono::milliseconds {100}
    );
    bridge.complete_error(
        ErrorResponse {
            .token = error_token,
            .capability = "missing.snapshot",
            .status = 404,
            .message = "missing",
        }
    );
    auto response =
        bridge.wait_for_response(error_token, std::chrono::milliseconds {1});
    REQUIRE(response);
    REQUIRE(response->status == 404);
    REQUIRE_FALSE(response->binary);
    REQUIRE(response->text.find("missing") != std::string::npos);
    REQUIRE(
        response->text.find("\"capability\":\"missing.snapshot\"") !=
        std::string::npos
    );
}

TEST_CASE("Bridge wraps snapshot responses", "[devtools]") {
    Bridge bridge;

    auto token = bridge.enqueue_snapshot_request(
        "rendering.render_graph",
        0,
        true,
        std::chrono::milliseconds {100}
    );

    bridge.publish_snapshot(
        SnapshotResponse {
            .token = token,
            .capability = "rendering.render_graph",
            .json = R"({"passes":[]})",
            .schema = "rendering.render_graph.v1",
            .version = 12,
        }
    );

    auto response =
        bridge.wait_for_response(token, std::chrono::milliseconds {1});
    REQUIRE(response);
    REQUIRE(response->status == 200);
    REQUIRE(response->content_type == "application/json");
    REQUIRE(
        response->text.find("\"id\":\"rendering.render_graph\"") !=
        std::string::npos
    );
    REQUIRE(
        response->text.find("\"schema\":\"rendering.render_graph.v1\"") !=
        std::string::npos
    );
    REQUIRE(response->text.find("\"version\":12") != std::string::npos);
    REQUIRE(
        response->text.find("\"data\":{\"passes\":[]}") != std::string::npos
    );

    auto cached = bridge.cached_snapshot("rendering.render_graph", 11, false);
    REQUIRE(cached);
    auto envelope = snapshot_envelope_json(*cached);
    REQUIRE(
        envelope.find("\"id\":\"rendering.render_graph\"") != std::string::npos
    );
    REQUIRE(envelope.find("\"data\":{\"passes\":[]}") != std::string::npos);
}

TEST_CASE("Bridge tracks active subscriptions in status", "[devtools]") {
    Bridge bridge;

    auto token = bridge.start_subscription("rendering.frame");
    auto status = bridge.status_json();
    REQUIRE(
        status.find("\"subscriptions\":{\"rendering.frame\":1}") !=
        std::string::npos
    );

    auto changes = bridge.take_subscription_changes();
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].start);
    REQUIRE(changes[0].token == token);
    REQUIRE(changes[0].capability == "rendering.frame");

    bridge.stop_subscription(token);
    status = bridge.status_json();
    REQUIRE(status.find("\"subscriptions\":{}") != std::string::npos);
}

TEST_CASE("Bridge stores manifest JSON", "[devtools]") {
    Bridge bridge;
    bridge.update_manifest({
        ManifestEntry {
            .id = "rendering.frame",
            .label = "Rendered Frame",
            .kind = "blob",
            .mime = "image/jpeg",
            .mode = PublishMode::OnDemand,
            .waitable = true,
        },
        ManifestEntry {
            .id = "input.clear",
            .label = "Clear Input",
            .kind = "command",
            .schema = "input.clear.v1",
            .request_type = "fei::devtools::input::ClearCommandBody",
            .response_type = "fei::devtools::input::ClearCommandResponse",
            .mode = PublishMode::OnDemand,
            .waitable = true,
        },
    });

    auto manifest = bridge.manifest_json();
    REQUIRE(manifest.find("rendering.frame") != std::string::npos);
    REQUIRE(manifest.find("on_demand") != std::string::npos);
    REQUIRE(
        manifest.find("\"schemas\":\"/api/v1/schemas\"") != std::string::npos
    );
    REQUIRE(
        manifest.find("\"get\":\"/api/v1/blobs/rendering.frame\"") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find("\"stream\":\"/api/v1/blobs/rendering.frame/stream\"") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find("\"params\":[\"after\",\"fresh\",\"timeout_ms\"]") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find("\"post\":\"/api/v1/commands/input.clear\"") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find(
            "\"request_type\":\"fei::devtools::input::ClearCommandBody\""
        ) != std::string::npos
    );
    REQUIRE(
        manifest.find(
            "\"response_type\":"
            "\"fei::devtools::input::ClearCommandResponse\""
        ) != std::string::npos
    );

    auto capability = bridge.find_capability("rendering.frame");
    REQUIRE(capability);
    REQUIRE(capability->kind == "blob");
    REQUIRE_FALSE(bridge.find_capability("missing"));

    const std::string schemas =
        R"({"version":1,"roots":["test.Root"],"types":{}})";
    bridge.update_schema_json(schemas);
    REQUIRE(bridge.schema_json() == schemas);
}
