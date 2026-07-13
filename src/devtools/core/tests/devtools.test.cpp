#include "devtools/bridge.hpp"
#include "ui_assets.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
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
        "rendering.render_schedule",
        0,
        true,
        std::chrono::milliseconds {100}
    );

    bridge.publish_snapshot(
        SnapshotResponse {
            .token = token,
            .capability = "rendering.render_schedule",
            .json = R"({"passes":[]})",
            .schema = "rendering.render_schedule.v1",
            .version = 12,
        }
    );

    auto response =
        bridge.wait_for_response(token, std::chrono::milliseconds {1});
    REQUIRE(response);
    REQUIRE(response->status == 200);
    REQUIRE(response->content_type == "application/json");
    REQUIRE(
        response->text.find("\"id\":\"rendering.render_schedule\"") !=
        std::string::npos
    );
    REQUIRE(
        response->text.find("\"schema\":\"rendering.render_schedule.v1\"") !=
        std::string::npos
    );
    REQUIRE(response->text.find("\"version\":12") != std::string::npos);
    REQUIRE(
        response->text.find("\"data\":{\"passes\":[]}") != std::string::npos
    );

    auto cached =
        bridge.cached_snapshot("rendering.render_schedule", 11, false);
    REQUIRE(cached);
    auto envelope = snapshot_envelope_json(*cached);
    REQUIRE(
        envelope.find("\"id\":\"rendering.render_schedule\"") !=
        std::string::npos
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

TEST_CASE("DevTools embeds its schema-driven web UI", "[devtools][ui]") {
    using namespace fei::devtools::detail;

    auto discovery = nlohmann::json::parse(c_discovery_json);
    REQUIRE(discovery.at("name") == "fei-devtools");
    REQUIRE(discovery.at("version") == 1);
    REQUIRE(discovery.at("manifest") == "/api/v1/manifest");
    REQUIRE(discovery.at("schemas") == "/api/v1/schemas");
    REQUIRE(discovery.at("status") == "/api/v1/status");
    REQUIRE(discovery.at("ui") == "/ui/");

    auto index = find_ui_asset("/ui/");
    REQUIRE(index);
    REQUIRE(index->content_type == "text/html; charset=utf-8");
    REQUIRE(index->content.find("FEI DevTools") != std::string_view::npos);
    REQUIRE(index->content.find("/ui/app.js") != std::string_view::npos);
    REQUIRE(
        index->content.find(R"(id="refresh-devtools")") !=
        std::string_view::npos
    );
    REQUIRE(index->content.find("refresh-status") == std::string_view::npos);
    REQUIRE(index->content.find("refresh-manifest") == std::string_view::npos);
    REQUIRE(index->content.find("command-confirm") == std::string_view::npos);
    REQUIRE(
        index->content.find(R"(data-grouping="namespace")") !=
        std::string_view::npos
    );
    REQUIRE(
        index->content.find(R"(data-grouping="kind")") != std::string_view::npos
    );

    auto styles = find_ui_asset("/ui/app.css");
    REQUIRE(styles);
    REQUIRE(styles->content_type == "text/css; charset=utf-8");
    REQUIRE_FALSE(styles->content.empty());
    REQUIRE(styles->content.find(".data-search") != std::string_view::npos);
    REQUIRE(
        styles->content.find(".collection-summary") != std::string_view::npos
    );
    REQUIRE(styles->content.find(".field-row:has") != std::string_view::npos);

    auto script = find_ui_asset("/ui/app.js");
    REQUIRE(script);
    REQUIRE(script->content_type == "text/javascript; charset=utf-8");
    REQUIRE(
        script->content.find("manifest.capabilities") != std::string_view::npos
    );
    REQUIRE(script->content.find("rendering.frame") == std::string_view::npos);
    REQUIRE(script->content.find("input.key") == std::string_view::npos);
    REQUIRE(script->content.find("/stream") == std::string_view::npos);
    REQUIRE(
        script->content.find(R"(capability.mode === "cached")") !=
        std::string_view::npos
    );
    REQUIRE(
        script->content.find("snapshotAutoLoadAttempted") !=
        std::string_view::npos
    );
    REQUIRE(
        script->content.find("const actionButton") != std::string_view::npos
    );
    REQUIRE(
        script->content.find("const freshButton") == std::string_view::npos
    );
    REQUIRE(script->content.find("Force refresh") == std::string_view::npos);
    REQUIRE(script->content.find("Load cached") == std::string_view::npos);
    REQUIRE(script->content.find("confirmCommand") == std::string_view::npos);
    REQUIRE(script->content.find("Review command") == std::string_view::npos);
    REQUIRE(script->content.find("Search data") != std::string_view::npos);
    REQUIRE(
        script->content.find("data-search-alias") != std::string_view::npos
    );
    REQUIRE(script->content.find("Expand all") != std::string_view::npos);
    REQUIRE(
        script->content.find("capabilityNamespace") != std::string_view::npos
    );
    REQUIRE(
        script->content.find(R"(viewMode === "raw")") != std::string_view::npos
    );
    REQUIRE(
        script->content.find("capability-details") != std::string_view::npos
    );
    REQUIRE(script->content.find("Snapshot data") == std::string_view::npos);

    REQUIRE_FALSE(find_ui_asset("/ui/missing.js"));
    REQUIRE(
        c_ui_content_security_policy.find("connect-src 'self'") !=
        std::string_view::npos
    );
    REQUIRE(
        c_ui_content_security_policy.find("frame-ancestors 'none'") !=
        std::string_view::npos
    );
}

TEST_CASE(
    "Manifest accepts capabilities unknown to the UI",
    "[devtools][ui][manifest]"
) {
    Bridge bridge;
    bridge.update_manifest({
        ManifestEntry {
            .id = "fixture.experimental",
            .label = "Experimental Fixture",
            .kind = "snapshot",
            .schema = "fixture.experimental.v1",
            .data_type = "fixture::ExperimentalSnapshot",
            .mode = PublishMode::Cached,
            .waitable = true,
        },
    });

    auto manifest = nlohmann::json::parse(bridge.manifest_json());
    const auto& capability = manifest.at("capabilities").at(0);
    REQUIRE(capability.at("id") == "fixture.experimental");
    REQUIRE(capability.at("kind") == "snapshot");
    REQUIRE(
        capability.at("endpoints").at("get") ==
        "/api/v1/snapshots/fixture.experimental"
    );
    REQUIRE(bridge.find_capability("fixture.experimental"));
}
