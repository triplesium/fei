#include "devtools/bridge.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>

namespace fei::devtools {

namespace {

using Json = nlohmann::json;

std::string publish_mode_name(PublishMode mode) {
    switch (mode) {
        case PublishMode::Cached:
            return "cached";
        case PublishMode::OnDemand:
            return "on_demand";
        case PublishMode::Streaming:
            return "streaming";
    }
    return "unknown";
}

Json metadata_to_json(
    const std::unordered_map<std::string, std::string>& metadata
) {
    Json result = Json::object();
    for (const auto& [key, value] : metadata) {
        result[key] = value;
    }
    return result;
}

BridgeResponse
text_response(int status, std::string content_type, std::string text) {
    return BridgeResponse {
        .status = status,
        .content_type = std::move(content_type),
        .text = std::move(text),
        .binary = false,
    };
}

BridgeResponse blob_response(const BridgeBlob& blob) {
    return BridgeResponse {
        .status = 200,
        .content_type = blob.mime,
        .headers =
            {
                {"X-DevTools-Capability", blob.capability},
                {"X-DevTools-Version", std::to_string(blob.version)},
                {"X-DevTools-Metadata", blob_metadata_json(blob)},
            },
        .bytes = blob.bytes,
        .binary = true,
    };
}

Json endpoints_for(const ManifestEntry& entry) {
    auto id = entry.id;
    if (entry.kind == "blob") {
        return Json {
            {"get", "/api/v1/blobs/" + id},
            {"stream", "/api/v1/blobs/" + id + "/stream"},
        };
    }
    if (entry.kind == "snapshot") {
        return Json {
            {"get", "/api/v1/snapshots/" + id},
        };
    }
    if (entry.kind == "command") {
        return Json {
            {"post", "/api/v1/commands/" + id},
        };
    }
    return Json::object();
}

Json params_for(const ManifestEntry& entry) {
    if (entry.kind == "blob" || entry.kind == "snapshot") {
        return Json::array({"after", "fresh", "timeout_ms"});
    }
    if (entry.kind == "command") {
        return Json::array({"timeout_ms"});
    }
    return Json::array();
}

} // namespace

struct Bridge::State {
    mutable std::mutex mutex;
    std::condition_variable response_available;
    std::condition_variable blob_available;
    Token next_token {1};
    std::vector<PendingRequest> pending_requests;
    std::vector<SubscriptionChange> subscription_changes;
    std::unordered_map<Token, std::string> active_subscriptions;
    std::unordered_map<Token, BridgeResponse> responses;
    std::unordered_map<std::string, BridgeBlob> blobs;
    std::unordered_map<std::string, BridgeSnapshot> snapshots;
    std::vector<ManifestEntry> manifest;
};

Bridge::Bridge() : m_state(std::make_shared<State>()) {}

Token Bridge::enqueue_blob_request(
    std::string capability,
    uint64 after,
    bool fresh,
    std::chrono::milliseconds timeout
) {
    std::scoped_lock lock(m_state->mutex);
    const auto token = m_state->next_token++;
    m_state->pending_requests.push_back(
        PendingRequest {
            .token = token,
            .kind = RequestKind::Blob,
            .capability = std::move(capability),
            .after = after,
            .fresh = fresh,
            .deadline = std::chrono::steady_clock::now() + timeout,
        }
    );
    return token;
}

Token Bridge::enqueue_snapshot_request(
    std::string capability,
    uint64 after,
    bool fresh,
    std::chrono::milliseconds timeout
) {
    std::scoped_lock lock(m_state->mutex);
    const auto token = m_state->next_token++;
    m_state->pending_requests.push_back(
        PendingRequest {
            .token = token,
            .kind = RequestKind::Snapshot,
            .capability = std::move(capability),
            .after = after,
            .fresh = fresh,
            .deadline = std::chrono::steady_clock::now() + timeout,
        }
    );
    return token;
}

Token Bridge::enqueue_command_request(
    std::string capability,
    std::string body,
    std::chrono::milliseconds timeout
) {
    std::scoped_lock lock(m_state->mutex);
    const auto token = m_state->next_token++;
    m_state->pending_requests.push_back(
        PendingRequest {
            .token = token,
            .kind = RequestKind::Command,
            .capability = std::move(capability),
            .body = std::move(body),
            .deadline = std::chrono::steady_clock::now() + timeout,
        }
    );
    return token;
}

Token Bridge::start_subscription(std::string capability) {
    std::scoped_lock lock(m_state->mutex);
    const auto token = m_state->next_token++;
    m_state->active_subscriptions.emplace(token, capability);
    m_state->subscription_changes.push_back(
        SubscriptionChange {
            .start = true,
            .token = token,
            .capability = std::move(capability),
        }
    );
    return token;
}

void Bridge::stop_subscription(Token token) {
    std::scoped_lock lock(m_state->mutex);
    m_state->active_subscriptions.erase(token);
    m_state->subscription_changes.push_back(
        SubscriptionChange {
            .start = false,
            .token = token,
        }
    );
}

std::vector<PendingRequest> Bridge::take_pending_requests() {
    std::scoped_lock lock(m_state->mutex);
    std::vector<PendingRequest> requests;
    requests.swap(m_state->pending_requests);
    return requests;
}

std::vector<SubscriptionChange> Bridge::take_subscription_changes() {
    std::scoped_lock lock(m_state->mutex);
    std::vector<SubscriptionChange> changes;
    changes.swap(m_state->subscription_changes);
    return changes;
}

Optional<BridgeResponse>
Bridge::wait_for_response(Token token, std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_state->mutex);
    m_state->response_available.wait_for(lock, timeout, [&]() {
        return m_state->responses.contains(token);
    });

    auto iter = m_state->responses.find(token);
    if (iter == m_state->responses.end()) {
        return nullopt;
    }

    auto response = std::move(iter->second);
    m_state->responses.erase(iter);
    return response;
}

Optional<BridgeBlob> Bridge::wait_for_blob_after(
    const std::string& capability,
    uint64 after,
    std::chrono::milliseconds timeout
) {
    std::unique_lock lock(m_state->mutex);
    m_state->blob_available.wait_for(lock, timeout, [&]() {
        auto iter = m_state->blobs.find(capability);
        return iter != m_state->blobs.end() && iter->second.version > after;
    });

    auto iter = m_state->blobs.find(capability);
    if (iter == m_state->blobs.end() || iter->second.version <= after) {
        return nullopt;
    }
    return iter->second;
}

Optional<BridgeBlob> Bridge::cached_blob(
    const std::string& capability,
    uint64 after,
    bool fresh
) const {
    if (fresh) {
        return nullopt;
    }

    std::scoped_lock lock(m_state->mutex);
    auto iter = m_state->blobs.find(capability);
    if (iter == m_state->blobs.end() || iter->second.version <= after) {
        return nullopt;
    }
    return iter->second;
}

Optional<BridgeSnapshot> Bridge::cached_snapshot(
    const std::string& capability,
    uint64 after,
    bool fresh
) const {
    if (fresh) {
        return nullopt;
    }

    std::scoped_lock lock(m_state->mutex);
    auto iter = m_state->snapshots.find(capability);
    if (iter == m_state->snapshots.end() || iter->second.version <= after) {
        return nullopt;
    }
    return iter->second;
}

void Bridge::publish_blob(BlobResponse response) {
    std::scoped_lock lock(m_state->mutex);
    auto& cached = m_state->blobs[response.capability];
    auto version = response.version;
    if (version == 0) {
        version = cached.version + 1;
    }
    cached = BridgeBlob {
        .capability = response.capability,
        .bytes = std::move(response.bytes),
        .mime = std::move(response.mime),
        .version = version,
        .metadata = std::move(response.metadata),
    };

    if (response.token != 0) {
        m_state->responses[response.token] = blob_response(cached);
    }
    m_state->response_available.notify_all();
    m_state->blob_available.notify_all();
}

void Bridge::publish_snapshot(SnapshotResponse response) {
    std::scoped_lock lock(m_state->mutex);
    auto& cached = m_state->snapshots[response.capability];
    auto version = response.version;
    if (version == 0) {
        version = cached.version + 1;
    }
    cached = BridgeSnapshot {
        .capability = response.capability,
        .json = std::move(response.json),
        .schema = std::move(response.schema),
        .version = version,
    };

    if (response.token != 0) {
        m_state->responses[response.token] = text_response(
            200,
            "application/json",
            snapshot_envelope_json(cached)
        );
    }
    m_state->response_available.notify_all();
}

void Bridge::complete_command(CommandResponse response) {
    std::scoped_lock lock(m_state->mutex);
    if (response.token != 0) {
        m_state->responses[response.token] =
            text_response(200, "application/json", std::move(response.json));
    }
    m_state->response_available.notify_all();
}

void Bridge::complete_error(ErrorResponse response) {
    std::scoped_lock lock(m_state->mutex);
    if (response.token != 0) {
        m_state->responses[response.token] = text_response(
            response.status,
            "application/json",
            error_json(
                std::move(response.message),
                response.status,
                std::move(response.capability)
            )
        );
    }
    m_state->response_available.notify_all();
}

void Bridge::update_manifest(std::vector<ManifestEntry> entries) {
    std::scoped_lock lock(m_state->mutex);
    m_state->manifest = std::move(entries);
}

Optional<ManifestEntry> Bridge::find_capability(const std::string& id) const {
    std::scoped_lock lock(m_state->mutex);
    for (const auto& entry : m_state->manifest) {
        if (entry.id == id) {
            return entry;
        }
    }
    return nullopt;
}

std::string Bridge::manifest_json() const {
    std::scoped_lock lock(m_state->mutex);
    Json entries = Json::array();
    for (const auto& entry : m_state->manifest) {
        Json value {
            {"id", entry.id},
            {"label", entry.label},
            {"kind", entry.kind},
            {"mode", publish_mode_name(entry.mode)},
            {"waitable", entry.waitable},
            {"endpoints", endpoints_for(entry)},
            {"params", params_for(entry)},
        };
        if (!entry.mime.empty()) {
            value["mime"] = entry.mime;
        }
        if (!entry.schema.empty()) {
            value["schema"] = entry.schema;
        }
        entries.push_back(std::move(value));
    }

    return Json {
        {"version", 1},
        {"capabilities", std::move(entries)},
    }
        .dump();
}

std::string Bridge::status_json() const {
    std::scoped_lock lock(m_state->mutex);
    Json blobs = Json::object();
    for (const auto& [id, blob] : m_state->blobs) {
        blobs[id] = Json {
            {"version", blob.version},
            {"mime", blob.mime},
            {"bytes", blob.bytes.size()},
            {"metadata", metadata_to_json(blob.metadata)},
        };
    }

    Json snapshots = Json::object();
    for (const auto& [id, snapshot] : m_state->snapshots) {
        snapshots[id] = Json {
            {"version", snapshot.version},
            {"schema", snapshot.schema},
            {"bytes", snapshot.json.size()},
        };
    }

    Json subscriptions = Json::object();
    for (const auto& [token, capability] : m_state->active_subscriptions) {
        (void)token;
        auto count = subscriptions.value(capability, 0);
        subscriptions[capability] = count + 1;
    }

    return Json {
        {"version", 1},
        {"pending_requests", m_state->pending_requests.size()},
        {"capabilities", m_state->manifest.size()},
        {"subscriptions", std::move(subscriptions)},
        {"blobs", std::move(blobs)},
        {"snapshots", std::move(snapshots)},
    }
        .dump();
}

std::string blob_metadata_json(const BridgeBlob& blob) {
    return Json {
        {"capability", blob.capability},
        {"version", blob.version},
        {"mime", blob.mime},
        {"bytes", blob.bytes.size()},
        {"metadata", metadata_to_json(blob.metadata)},
    }
        .dump();
}

std::string snapshot_envelope_json(const BridgeSnapshot& snapshot) {
    auto data = Json::parse(snapshot.json, nullptr, false);
    if (data.is_discarded()) {
        data = snapshot.json;
    }
    return Json {
        {"id", snapshot.capability},
        {"schema", snapshot.schema},
        {"version", snapshot.version},
        {"data", std::move(data)},
    }
        .dump();
}

std::string error_json(
    std::string message,
    int status,
    std::string capability,
    std::string kind
) {
    Json result {
        {"error", std::move(message)},
        {"status", status},
    };
    if (!capability.empty()) {
        result["capability"] = std::move(capability);
    }
    if (!kind.empty()) {
        result["kind"] = std::move(kind);
    }
    return result.dump();
}

} // namespace fei::devtools
