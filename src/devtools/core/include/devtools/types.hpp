#pragma once

#include "base/optional.hpp"
#include "base/types.hpp"
#include "refl/reflect.hpp"
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

enum class ProtocolKind : uint8 {
    Blob,
    Json,
};

struct Config {
    std::string host {"127.0.0.1"};
    uint16 port {8080};
    std::string artifact_dir;
};

struct Capability {
    std::string id;
    std::string label;
};

struct FEI_REFLECT BlobRef {
    std::string capability;
};

struct BlobProtocol {
    std::string mime;
    PublishMode mode {PublishMode::OnDemand};
    bool waitable {true};
};

struct JsonProtocol {
    std::string schema;
    Optional<TypeId> request_type;
    Optional<TypeId> response_type;
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

struct JsonRequest {
    Optional<std::string> body;
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

struct JsonResponse {
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
