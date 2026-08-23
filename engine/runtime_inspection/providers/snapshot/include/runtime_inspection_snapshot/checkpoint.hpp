#pragma once

#include "base/result.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "snapshot/archive.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ets {

class World;

namespace runtime_inspection::checkpoint {

struct CheckpointRequest {
    std::string name;
    bool strict {false};
};

struct CheckpointListRequest {};

struct CheckpointAuditRequest {};

struct CheckpointClearRequest {};

struct CheckpointFileRequest {
    std::string name;
    std::string path;
};

struct CheckpointFileResponse {
    snapshot::CheckpointInfo checkpoint;
    std::string path;
    std::size_t file_size {};
};

struct CheckpointRestoreResponse {
    std::string name;
    std::size_t restored_entity_count {};
};

struct CheckpointDeleteResponse {
    std::string name;
};

struct CheckpointClearResponse {
    std::size_t removed {};
};

class CreateCheckpointProvider {
  public:
    using Request = CheckpointRequest;
    using Response = snapshot::CheckpointInfo;

    static constexpr std::string_view id {"play.checkpoint.create"};
    static constexpr std::string_view label {"Create Play Checkpoint"};
    static constexpr std::string_view description {
        "Capture the current ECS world into a named in-memory checkpoint."
    };
    static constexpr std::string_view schema {"play.checkpoint.create.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {R"json({
        "type":"object",
        "additionalProperties":false,
        "required":["name"],
        "properties":{
            "name":{"type":"string","pattern":"^[A-Za-z0-9_-]{1,64}$"},
            "strict":{"type":"boolean"}
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object",
        "additionalProperties":false,
        "required":["name","revision","entity_count","resource_count","byte_size"],
        "properties":{
            "name":{"type":"string"},
            "revision":{"type":"integer","minimum":1},
            "entity_count":{"type":"integer","minimum":0},
            "resource_count":{"type":"integer","minimum":0},
            "byte_size":{"type":"integer","minimum":0}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class ExportCheckpointProvider {
  public:
    using Request = CheckpointFileRequest;
    using Response = CheckpointFileResponse;

    static constexpr std::string_view id {"play.checkpoint.export"};
    static constexpr std::string_view label {"Export Play Checkpoint"};
    static constexpr std::string_view description {
        "Persist a named in-memory checkpoint to a local snapshot archive."
    };
    static constexpr std::string_view schema {"play.checkpoint.export.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {R"json({
        "type":"object",
        "additionalProperties":false,
        "required":["name","path"],
        "properties":{
            "name":{"type":"string","pattern":"^[A-Za-z0-9_-]{1,64}$"},
            "path":{"type":"string","minLength":1,"maxLength":4096}
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object",
        "additionalProperties":false,
        "required":["name","revision","entity_count","resource_count","byte_size","path","file_size"],
        "properties":{
            "name":{"type":"string"},
            "revision":{"type":"integer","minimum":1},
            "entity_count":{"type":"integer","minimum":0},
            "resource_count":{"type":"integer","minimum":0},
            "byte_size":{"type":"integer","minimum":0},
            "path":{"type":"string"},
            "file_size":{"type":"integer","minimum":0}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class ImportCheckpointProvider {
  public:
    using Request = CheckpointFileRequest;
    using Response = CheckpointFileResponse;

    static constexpr std::string_view id {"play.checkpoint.import"};
    static constexpr std::string_view label {"Import Play Checkpoint"};
    static constexpr std::string_view description {
        "Load a compatible local snapshot archive into the in-memory "
        "checkpoint store."
    };
    static constexpr std::string_view schema {"play.checkpoint.import.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {
        ExportCheckpointProvider::request_schema_json
    };
    static constexpr std::string_view response_schema_json {
        ExportCheckpointProvider::response_schema_json
    };

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class ListCheckpointsProvider {
  public:
    using Request = CheckpointListRequest;
    using Response = std::vector<snapshot::CheckpointInfo>;

    static constexpr std::string_view id {"play.checkpoint.list"};
    static constexpr std::string_view label {"List Play Checkpoints"};
    static constexpr std::string_view description {
        "List named in-memory ECS checkpoints available in this runtime."
    };
    static constexpr std::string_view schema {"play.checkpoint.list.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        R"json({"type":"object","additionalProperties":false})json"
    };
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object",
        "additionalProperties":false,
        "required":["checkpoints"],
        "properties":{
            "checkpoints":{"type":"array"}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class RestoreCheckpointProvider {
  public:
    using Request = CheckpointRequest;
    using Response = CheckpointRestoreResponse;

    static constexpr std::string_view id {"play.checkpoint.restore"};
    static constexpr std::string_view label {"Restore Play Checkpoint"};
    static constexpr std::string_view description {
        "Replace the current ECS entities and registered resources with a "
        "named in-memory checkpoint."
    };
    static constexpr std::string_view schema {"play.checkpoint.restore.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {
        CreateCheckpointProvider::request_schema_json
    };
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object",
        "additionalProperties":false,
        "required":["name","restored_entity_count"],
        "properties":{
            "name":{"type":"string"},
            "restored_entity_count":{"type":"integer","minimum":0}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class AuditCheckpointProvider {
  public:
    using Request = CheckpointAuditRequest;
    using Response = snapshot::SnapshotAudit;

    static constexpr std::string_view id {"play.checkpoint.audit"};
    static constexpr std::string_view label {"Audit Play Checkpoints"};
    static constexpr std::string_view description {
        "Report snapshot, ignored, rebuilt, derived, and unregistered ECS "
        "state before an agent relies on retry."
    };
    static constexpr std::string_view schema {"play.checkpoint.audit.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {
        R"json({"type":"object","additionalProperties":false})json"
    };
    static constexpr std::string_view response_schema_json {
        R"json({"type":"object"})json"
    };

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class DeleteCheckpointProvider {
  public:
    using Request = CheckpointRequest;
    using Response = CheckpointDeleteResponse;

    static constexpr std::string_view id {"play.checkpoint.delete"};
    static constexpr std::string_view label {"Delete Play Checkpoint"};
    static constexpr std::string_view description {
        "Delete one named in-memory checkpoint."
    };
    static constexpr std::string_view schema {"play.checkpoint.delete.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        CreateCheckpointProvider::request_schema_json
    };
    static constexpr std::string_view response_schema_json {
        R"json({"type":"object"})json"
    };

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class ClearCheckpointsProvider {
  public:
    using Request = CheckpointClearRequest;
    using Response = CheckpointClearResponse;

    static constexpr std::string_view id {"play.checkpoint.clear"};
    static constexpr std::string_view label {"Clear Play Checkpoints"};
    static constexpr std::string_view description {
        "Delete every in-memory checkpoint."
    };
    static constexpr std::string_view schema {"play.checkpoint.clear.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        R"json({"type":"object","additionalProperties":false})json"
    };
    static constexpr std::string_view response_schema_json {
        R"json({"type":"object"})json"
    };

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

static_assert(InspectionProvider<CreateCheckpointProvider>);
static_assert(InspectionProvider<ExportCheckpointProvider>);
static_assert(InspectionProvider<ImportCheckpointProvider>);
static_assert(InspectionProvider<ListCheckpointsProvider>);
static_assert(InspectionProvider<RestoreCheckpointProvider>);
static_assert(InspectionProvider<AuditCheckpointProvider>);
static_assert(InspectionProvider<DeleteCheckpointProvider>);
static_assert(InspectionProvider<ClearCheckpointsProvider>);

[[nodiscard]] Result<std::string, InspectionError>
create_checkpoint_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
export_checkpoint_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
import_checkpoint_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
list_checkpoints_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
restore_checkpoint_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
audit_checkpoint_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
delete_checkpoint_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
clear_checkpoints_json(World& world, std::string_view request_json);

Status<InspectionError>
register_checkpoint_inspection_providers(InspectionRegistry& registry);

} // namespace runtime_inspection::checkpoint
} // namespace ets
