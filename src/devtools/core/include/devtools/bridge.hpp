#pragma once

#include "base/optional.hpp"
#include "devtools/types.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei::devtools {

struct ManifestEntry {
    std::string id;
    std::string label;
    std::string kind;
    std::string mime;
    std::string schema;
    std::string data_type;
    std::string request_type;
    std::string response_type;
    PublishMode mode {PublishMode::Cached};
    bool waitable {false};
};

struct BridgeBlob {
    std::string capability;
    std::vector<byte> bytes;
    std::string mime;
    uint64 version {0};
    std::unordered_map<std::string, std::string> metadata;

    bool empty() const { return bytes.empty(); }
};

struct BridgeSnapshot {
    std::string capability;
    std::string json;
    std::string schema;
    uint64 version {0};

    bool empty() const { return json.empty(); }
};

struct BridgeResponse {
    int status {200};
    std::string content_type {"application/json"};
    std::unordered_map<std::string, std::string> headers;
    std::string text;
    std::vector<byte> bytes;
    bool binary {false};
};

struct PendingRequest {
    Token token {0};
    RequestKind kind {RequestKind::Blob};
    std::string capability;
    uint64 after {0};
    bool fresh {false};
    std::string body;
    std::chrono::steady_clock::time_point deadline;
};

struct SubscriptionChange {
    bool start {true};
    Token token {0};
    std::string capability;
};

class Bridge {
  public:
    Bridge();

    Token enqueue_blob_request(
        std::string capability,
        uint64 after,
        bool fresh,
        std::chrono::milliseconds timeout
    );
    Token enqueue_snapshot_request(
        std::string capability,
        uint64 after,
        bool fresh,
        std::chrono::milliseconds timeout
    );
    Token enqueue_command_request(
        std::string capability,
        std::string body,
        std::chrono::milliseconds timeout
    );

    Token start_subscription(std::string capability);
    void stop_subscription(Token token);

    std::vector<PendingRequest> take_pending_requests();
    std::vector<SubscriptionChange> take_subscription_changes();

    Optional<BridgeResponse>
    wait_for_response(Token token, std::chrono::milliseconds timeout);
    Optional<BridgeBlob> wait_for_blob_after(
        const std::string& capability,
        uint64 after,
        std::chrono::milliseconds timeout
    );

    Optional<BridgeBlob>
    cached_blob(const std::string& capability, uint64 after, bool fresh) const;
    Optional<BridgeSnapshot> cached_snapshot(
        const std::string& capability,
        uint64 after,
        bool fresh
    ) const;

    void publish_blob(BlobResponse response);
    void publish_snapshot(SnapshotResponse response);
    void complete_command(CommandResponse response);
    void complete_error(ErrorResponse response);

    void update_manifest(std::vector<ManifestEntry> entries);
    void update_schema_json(std::string json);
    Optional<ManifestEntry> find_capability(const std::string& id) const;
    std::string manifest_json() const;
    std::string schema_json() const;
    std::string status_json() const;

  private:
    struct State;
    std::shared_ptr<State> m_state;
};

std::string blob_metadata_json(const BridgeBlob& blob);
std::string snapshot_envelope_json(const BridgeSnapshot& snapshot);
std::string error_json(
    std::string message,
    int status,
    std::string capability = {},
    std::string kind = {}
);

} // namespace fei::devtools
