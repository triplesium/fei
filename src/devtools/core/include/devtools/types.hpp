#pragma once

#include "base/types.hpp"
#include "refl/type.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei::devtools {

using Token = uint64;

enum class PublishMode : uint8 {
    Cached,
    OnDemand,
    Streaming,
};

enum class RequestKind : uint8 {
    Blob,
    Snapshot,
    Command,
};

struct Config {
    std::string host {"127.0.0.1"};
    uint16 port {8080};
    bool agent_mode {false};
    std::string artifact_dir;
};

struct Capability {
    std::string id;
    std::string label;
};

struct BlobCapability {
    std::string mime;
    PublishMode mode {PublishMode::OnDemand};
    bool waitable {true};
};

struct SnapshotCapability {
    std::string schema;
    TypeId data_type;
    PublishMode mode {PublishMode::Cached};
};

struct CommandCapability {
    std::string schema;
    TypeId request_type;
    TypeId response_type;
};

struct Request {
    Token token {0};
    std::string capability;
    std::chrono::steady_clock::time_point deadline;
    bool fresh {false};
};

struct BlobRequest {
    uint64 after {0};
};

struct SnapshotRequest {
    uint64 after {0};
};

struct CommandRequest {
    std::string body;
};

struct Subscription {
    Token token {0};
    std::string capability;
};

struct BlobResponse {
    Token token {0};
    std::string capability;
    std::vector<byte> bytes;
    std::string mime;
    uint64 version {0};
    std::unordered_map<std::string, std::string> metadata;
};

struct SnapshotResponse {
    Token token {0};
    std::string capability;
    std::string json;
    std::string schema;
    uint64 version {0};
};

struct CommandResponse {
    Token token {0};
    std::string capability;
    std::string json {"{}"};
};

struct ErrorResponse {
    Token token {0};
    std::string capability;
    int status {500};
    std::string message;
};

} // namespace fei::devtools
