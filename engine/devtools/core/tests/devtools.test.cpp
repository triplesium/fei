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
    REQUIRE(pending[0].protocol == ProtocolKind::Blob);
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

    auto token = bridge.enqueue_request(
        "input.clear",
        nullopt,
        std::chrono::milliseconds {1}
    );
    REQUIRE_FALSE(
        bridge.wait_for_response(token, std::chrono::milliseconds {1})
    );

    auto error_token = bridge.enqueue_request(
        "missing.capability",
        nullopt,
        std::chrono::milliseconds {100}
    );
    bridge.complete_error(
        ErrorResponse {
            .token = error_token,
            .capability = "missing.capability",
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
        response->text.find("\"capability\":\"missing.capability\"") !=
        std::string::npos
    );
}

TEST_CASE(
    "Bridge completes parameterless capabilities without an envelope",
    "[devtools]"
) {
    Bridge bridge;

    auto token = bridge.enqueue_request(
        "rendering.render_schedule",
        nullopt,
        std::chrono::milliseconds {100}
    );
    auto pending = bridge.take_pending_requests();
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].protocol == ProtocolKind::Json);
    REQUIRE_FALSE(pending[0].body);

    bridge.complete_response(
        JsonResponse {
            .token = token,
            .capability = "rendering.render_schedule",
            .json = R"({"passes":[]})",
        }
    );

    auto response =
        bridge.wait_for_response(token, std::chrono::milliseconds {1});
    REQUIRE(response);
    REQUIRE(response->status == 200);
    REQUIRE(response->content_type == "application/json");
    REQUIRE(response->text == R"({"passes":[]})");
}

TEST_CASE("Bridge queues and completes JSON capabilities", "[devtools]") {
    Bridge bridge;

    auto token = bridge.enqueue_request(
        "reflection.search",
        std::string {R"({"pattern":"Transform","limit":10})"},
        std::chrono::milliseconds {100}
    );
    auto pending = bridge.take_pending_requests();
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].token == token);
    REQUIRE(pending[0].protocol == ProtocolKind::Json);
    REQUIRE(pending[0].capability == "reflection.search");
    REQUIRE(pending[0].body);
    REQUIRE(pending[0].body->find("Transform") != std::string::npos);

    bridge.complete_response(
        JsonResponse {
            .token = token,
            .capability = "reflection.search",
            .json = R"({"matches":[],"truncated":false})",
        }
    );
    auto response =
        bridge.wait_for_response(token, std::chrono::milliseconds {1});
    REQUIRE(response);
    REQUIRE(response->status == 200);
    REQUIRE_FALSE(response->binary);
    REQUIRE(response->text.find("matches") != std::string::npos);
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
            .mime = "image/jpeg",
            .mode = PublishMode::OnDemand,
            .waitable = true,
            .endpoints =
                {
                    ManifestEndpoint {
                        .rel = "read",
                        .method = "GET",
                        .path = "/api/v1/capabilities/rendering.frame",
                        .params = {"after", "fresh", "timeout_ms"},
                    },
                    ManifestEndpoint {
                        .rel = "stream",
                        .method = "GET",
                        .path = "/api/v1/capabilities/rendering.frame/stream",
                        .params = {"after"},
                    },
                },
        },
        ManifestEntry {
            .id = "input.clear",
            .label = "Clear Input",
            .schema = "input.clear.v1",
            .response_type =
                std::string {"fei::devtools::input::ClearInputResponse"},
            .mode = PublishMode::OnDemand,
            .waitable = true,
            .endpoints =
                {
                    ManifestEndpoint {
                        .rel = "invoke",
                        .method = "POST",
                        .path = "/api/v1/capabilities/input.clear",
                        .params = {"timeout_ms"},
                    },
                },
        },
        ManifestEntry {
            .id = "rendering.render_schedule",
            .label = "Render Schedule",
            .schema = "rendering.render_schedule.v1",
            .response_type =
                std::string {
                    "fei::devtools::rendering::RenderScheduleSnapshot"
                },
            .mode = PublishMode::OnDemand,
            .waitable = true,
            .endpoints =
                {
                    ManifestEndpoint {
                        .rel = "invoke",
                        .method = "POST",
                        .path = "/api/v1/capabilities/"
                                "rendering.render_schedule",
                        .params = {"timeout_ms"},
                    },
                },
        },
        ManifestEntry {
            .id = "reflection.search",
            .label = "Search Reflected Types",
            .schema = "reflection.search.v1",
            .request_type =
                std::string {"fei::devtools::reflection::SearchRequest"},
            .response_type =
                std::string {"fei::devtools::reflection::SearchResponse"},
            .mode = PublishMode::OnDemand,
            .waitable = true,
            .endpoints = {
                ManifestEndpoint {
                    .rel = "invoke",
                    .method = "POST",
                    .path = "/api/v1/capabilities/reflection.search",
                    .params = {"timeout_ms"},
                },
            },
        },
    });

    auto manifest = bridge.manifest_json();
    REQUIRE(manifest.find("rendering.frame") != std::string::npos);
    REQUIRE(manifest.find("on_demand") != std::string::npos);
    REQUIRE(
        manifest.find("\"schemas\":\"/api/v1/schemas\"") != std::string::npos
    );
    REQUIRE(
        manifest.find("\"path\":\"/api/v1/capabilities/rendering.frame\"") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find(
            "\"path\":\"/api/v1/capabilities/rendering.frame/stream\""
        ) != std::string::npos
    );
    REQUIRE(
        manifest.find("\"params\":[\"after\",\"fresh\",\"timeout_ms\"]") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find("\"path\":\"/api/v1/capabilities/input.clear\"") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find("\"path\":\"/api/v1/capabilities/reflection.search\"") !=
        std::string::npos
    );
    REQUIRE(
        manifest.find(
            "\"path\":\"/api/v1/capabilities/"
            "rendering.render_schedule\""
        ) != std::string::npos
    );
    REQUIRE(
        manifest.find(
            "\"response_type\":"
            "\"fei::devtools::input::ClearInputResponse\""
        ) != std::string::npos
    );
    REQUIRE(manifest.find("\"kind\"") == std::string::npos);
    REQUIRE(manifest.find("\"rel\":\"invoke\"") != std::string::npos);
    REQUIRE(manifest.find("\"method\":\"POST\"") != std::string::npos);

    auto capability = bridge.find_capability("rendering.frame");
    REQUIRE(capability);
    REQUIRE(capability->endpoints.size() == 2);
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
    REQUIRE(index->content.find("data-grouping") == std::string_view::npos);
    REQUIRE(
        index->content.find(R"(id="toggle-blobs")") != std::string_view::npos
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
    REQUIRE(styles->content.find(".blob-reference") != std::string_view::npos);
    REQUIRE(
        styles->content.find(".sidebar-filter-button") != std::string_view::npos
    );
    REQUIRE(
        styles->content.find("clamp(140px, 20%, 220px)") !=
        std::string_view::npos
    );
    REQUIRE(
        styles->content.find("width: max-content") != std::string_view::npos
    );
    REQUIRE(
        styles->content.find(".data-table-column-text") !=
        std::string_view::npos
    );
    REQUIRE(
        styles->content.find(
            ".collection-disclosure[open] > .data-table-wrap"
        ) != std::string_view::npos
    );

    auto script = find_ui_asset("/ui/app.js");
    REQUIRE(script);
    REQUIRE(script->content_type == "text/javascript; charset=utf-8");
    REQUIRE(
        script->content.find("manifest.capabilities") != std::string_view::npos
    );
    REQUIRE(script->content.find("rendering.frame") == std::string_view::npos);
    REQUIRE(script->content.find("input.key") == std::string_view::npos);
    REQUIRE(script->content.find("/stream") == std::string_view::npos);
    REQUIRE(script->content.find("renderSnapshot") == std::string_view::npos);
    REQUIRE(
        script->content.find("snapshotAutoLoadAttempted") ==
        std::string_view::npos
    );
    REQUIRE(script->content.find("tableColumnClass") != std::string_view::npos);
    REQUIRE(
        script->content.find("ensureBlobUrl(capabilityState)") !=
        std::string_view::npos
    );
    REQUIRE(
        script->content.find(R"(case "blob_ref")") != std::string_view::npos
    );
    REQUIRE(
        script->content.find("renderBlobReference") != std::string_view::npos
    );
    REQUIRE(
        script->content.find("requestBlobCapability") != std::string_view::npos
    );
    REQUIRE(
        script->content.find("isBlobCapability(capability)") !=
        std::string_view::npos
    );
    REQUIRE(
        script->content.find("fei-devtools-sidebar-show-blobs") !=
        std::string_view::npos
    );
    REQUIRE(
        script->content.find(R"("$optional" in value)") !=
        std::string_view::npos
    );
    REQUIRE(
        script->content.find(
            "URL.createObjectURL(capabilityState.blob.blob)"
        ) != std::string_view::npos
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
    REQUIRE(
        script->content.find(R"(endpointByRel(capability, "invoke"))") !=
        std::string_view::npos
    );
    REQUIRE(script->content.find("capability.kind") == std::string_view::npos);
    REQUIRE(
        script->content.find("Array.isArray(capability.endpoints)") !=
        std::string_view::npos
    );

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
    "Manifest describes parameterless capabilities",
    "[devtools][manifest]"
) {
    Bridge bridge;
    bridge.update_manifest({
        ManifestEntry {
            .id = "fixture.experimental",
            .label = "Experimental Fixture",
            .schema = "fixture.experimental.v1",
            .response_type = std::string {"fixture::ExperimentalSnapshot"},
            .mode = PublishMode::OnDemand,
            .waitable = true,
            .endpoints = {
                ManifestEndpoint {
                    .rel = "invoke",
                    .method = "POST",
                    .path = "/api/v1/capabilities/fixture.experimental",
                    .params = {"timeout_ms"},
                },
            },
        },
    });

    auto manifest = nlohmann::json::parse(bridge.manifest_json());
    const auto& capability = manifest.at("capabilities").at(0);
    REQUIRE(capability.at("id") == "fixture.experimental");
    REQUIRE_FALSE(capability.contains("kind"));
    REQUIRE_FALSE(capability.contains("request_type"));
    REQUIRE(capability.at("response_type") == "fixture::ExperimentalSnapshot");
    REQUIRE(
        capability.at("endpoints").at(0).at("path") ==
        "/api/v1/capabilities/fixture.experimental"
    );
    REQUIRE(capability.at("endpoints").at(0).at("rel") == "invoke");
    REQUIRE(bridge.find_capability("fixture.experimental"));
}
