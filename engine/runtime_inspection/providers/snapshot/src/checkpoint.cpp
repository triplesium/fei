#include "runtime_inspection_snapshot/checkpoint.hpp"

#include "ecs/world.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/node.hpp"

#include <string>
#include <utility>

namespace fei::runtime_inspection::checkpoint {
namespace {

using serialization::SerializedField;
using serialization::SerializedNode;

InspectionError snapshot_error(const snapshot::SnapshotError& error) {
    auto kind = InspectionErrorKind::Conflict;
    switch (error.kind) {
        case snapshot::SnapshotError::Kind::InvalidCheckpointName:
            kind = InspectionErrorKind::InvalidRequest;
            break;
        case snapshot::SnapshotError::Kind::CheckpointNotFound:
            kind = InspectionErrorKind::NotFound;
            break;
        case snapshot::SnapshotError::Kind::CheckpointLimitReached:
            kind = InspectionErrorKind::Conflict;
            break;
        case snapshot::SnapshotError::Kind::TypeNotFound:
        case snapshot::SnapshotError::Kind::MissingResource:
        case snapshot::SnapshotError::Kind::InvalidEntityReference:
        case snapshot::SnapshotError::Kind::SerializeFailed:
        case snapshot::SnapshotError::Kind::StrictAuditFailed:
            kind = InspectionErrorKind::Unsupported;
            break;
        default:
            kind = InspectionErrorKind::Conflict;
            break;
    }
    return InspectionError {
        .kind = kind,
        .message = error.path.empty() ? error.message :
                                        error.path + ": " + error.message,
    };
}

Result<CheckpointRequest, InspectionError>
parse_checkpoint_request(std::string_view request_json) {
    auto node = serialization::read_json(request_json);
    if (!node) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = std::move(node.error().message),
            }
        );
    }
    const auto* object = node->try_object();
    if (object == nullptr || object->empty() || object->size() > 2) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Checkpoint request requires a name and optional "
                           "strict flag",
            }
        );
    }
    const auto* name = serialization::find_field(*object, "name");
    if (name == nullptr || !name->value.is_string()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Checkpoint name must be a string",
            }
        );
    }
    bool strict = false;
    if (const auto* strict_field =
            serialization::find_field(*object, "strict")) {
        const auto* strict_value = strict_field->value.try_bool();
        if (strict_value == nullptr) {
            return failure(
                InspectionError {
                    .kind = InspectionErrorKind::InvalidRequest,
                    .message = "Checkpoint strict flag must be a boolean",
                }
            );
        }
        strict = *strict_value;
    }
    for (const auto& field : *object) {
        if (field.name != "name" && field.name != "strict") {
            return failure(
                InspectionError {
                    .kind = InspectionErrorKind::InvalidRequest,
                    .message =
                        "Unknown checkpoint request field '" + field.name + "'",
                }
            );
        }
    }
    return CheckpointRequest {
        .name = *name->value.try_string(),
        .strict = strict,
    };
}

Status<InspectionError> parse_empty_request(std::string_view request_json) {
    auto node = serialization::read_json(request_json);
    if (!node) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = std::move(node.error().message),
            }
        );
    }
    const auto* object = node->try_object();
    if (object == nullptr || !object->empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Checkpoint request must be an empty object",
            }
        );
    }
    return {};
}

SerializedNode checkpoint_info_node(const snapshot::CheckpointInfo& info) {
    return SerializedNode::object({
        SerializedField {"name", SerializedNode::string(info.name)},
        SerializedField {
            "revision",
            SerializedNode::unsigned_integer(info.revision),
        },
        SerializedField {
            "entity_count",
            SerializedNode::unsigned_integer(info.entity_count),
        },
        SerializedField {
            "resource_count",
            SerializedNode::unsigned_integer(info.resource_count),
        },
        SerializedField {
            "byte_size",
            SerializedNode::unsigned_integer(info.byte_size),
        },
    });
}

SerializedNode audit_entry_node(const snapshot::SnapshotAuditEntry& entry) {
    return SerializedNode::object({
        SerializedField {"type", SerializedNode::string(entry.type_name)},
        SerializedField {
            "instances",
            SerializedNode::unsigned_integer(entry.instances),
        },
        SerializedField {
            "disposition",
            SerializedNode::string(
                std::string(snapshot::audit_disposition_name(entry.disposition))
            ),
        },
        SerializedField {
            "serializable",
            SerializedNode::boolean(entry.serializable),
        },
        SerializedField {"message", SerializedNode::string(entry.message)},
    });
}

Result<std::string, InspectionError> encode_node(SerializedNode node) {
    auto json = serialization::write_json(node, -1);
    if (!json) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Internal,
                .message = std::move(json.error().message),
            }
        );
    }
    return std::move(*json);
}

Result<snapshot::CheckpointStore&, InspectionError>
checkpoint_store(World& world) {
    if (!world.has_resource<snapshot::CheckpointStore>()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "Runtime checkpoint store is not installed",
            }
        );
    }
    return world.resource<snapshot::CheckpointStore>();
}

Result<const snapshot::CheckpointStore&, InspectionError>
checkpoint_store(const World& world) {
    if (!world.has_resource<snapshot::CheckpointStore>()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "Runtime checkpoint store is not installed",
            }
        );
    }
    return world.resource<snapshot::CheckpointStore>();
}

} // namespace

Result<CreateCheckpointProvider::Response, InspectionError>
CreateCheckpointProvider::inspect(
    World& world,
    const CheckpointRequest& request
) const {
    auto store = checkpoint_store(world);
    if (!store) {
        return failure(std::move(store.error()));
    }
    auto result = store->create(request.name, world, request.strict);
    if (!result) {
        return failure(snapshot_error(result.error()));
    }
    return std::move(*result);
}

Result<DeleteCheckpointProvider::Response, InspectionError>
DeleteCheckpointProvider::inspect(
    World& world,
    const CheckpointRequest& request
) const {
    auto store = checkpoint_store(world);
    if (!store) {
        return failure(std::move(store.error()));
    }
    if (!store->erase(request.name)) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::NotFound,
                .message = "Checkpoint '" + request.name + "' does not exist",
            }
        );
    }
    return CheckpointDeleteResponse {.name = request.name};
}

Result<ClearCheckpointsProvider::Response, InspectionError>
ClearCheckpointsProvider::inspect(
    World& world,
    const CheckpointClearRequest&
) const {
    auto store = checkpoint_store(world);
    if (!store) {
        return failure(std::move(store.error()));
    }
    return CheckpointClearResponse {.removed = store->clear()};
}

Result<ListCheckpointsProvider::Response, InspectionError>
ListCheckpointsProvider::inspect(
    World& world,
    const CheckpointListRequest&
) const {
    auto store = checkpoint_store(static_cast<const World&>(world));
    if (!store) {
        return failure(std::move(store.error()));
    }
    return store->list();
}

Result<RestoreCheckpointProvider::Response, InspectionError>
RestoreCheckpointProvider::inspect(
    World& world,
    const CheckpointRequest& request
) const {
    auto store = checkpoint_store(world);
    if (!store) {
        return failure(std::move(store.error()));
    }
    auto restored = store->restore(request.name, world);
    if (!restored) {
        return failure(snapshot_error(restored.error()));
    }
    return CheckpointRestoreResponse {
        .name = request.name,
        .restored_entity_count = restored->entities.size(),
    };
}

Result<AuditCheckpointProvider::Response, InspectionError>
AuditCheckpointProvider::inspect(
    World& world,
    const CheckpointAuditRequest&
) const {
    auto store = checkpoint_store(static_cast<const World&>(world));
    if (!store) {
        return failure(std::move(store.error()));
    }
    return snapshot::audit(static_cast<const World&>(world), store->registry());
}

Result<std::string, InspectionError>
create_checkpoint_json(World& world, std::string_view request_json) {
    auto request = parse_checkpoint_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = CreateCheckpointProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return encode_node(checkpoint_info_node(*response));
}

Result<std::string, InspectionError>
list_checkpoints_json(World& world, std::string_view request_json) {
    if (auto status = parse_empty_request(request_json); !status) {
        return failure(std::move(status.error()));
    }
    auto response =
        ListCheckpointsProvider {}.inspect(world, CheckpointListRequest {});
    if (!response) {
        return failure(std::move(response.error()));
    }
    SerializedNode::Array checkpoints;
    checkpoints.reserve(response->size());
    for (const auto& info : *response) {
        checkpoints.push_back(checkpoint_info_node(info));
    }
    return encode_node(
        SerializedNode::object({SerializedField {
            "checkpoints",
            SerializedNode::array(std::move(checkpoints)),
        }})
    );
}

Result<std::string, InspectionError>
restore_checkpoint_json(World& world, std::string_view request_json) {
    auto request = parse_checkpoint_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = RestoreCheckpointProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return encode_node(
        SerializedNode::object({
            SerializedField {"name", SerializedNode::string(response->name)},
            SerializedField {
                "restored_entity_count",
                SerializedNode::unsigned_integer(
                    response->restored_entity_count
                ),
            },
        })
    );
}

Result<std::string, InspectionError>
audit_checkpoint_json(World& world, std::string_view request_json) {
    if (auto status = parse_empty_request(request_json); !status) {
        return failure(std::move(status.error()));
    }
    auto response =
        AuditCheckpointProvider {}.inspect(world, CheckpointAuditRequest {});
    if (!response) {
        return failure(std::move(response.error()));
    }
    SerializedNode::Array components;
    components.reserve(response->components.size());
    for (const auto& entry : response->components) {
        components.push_back(audit_entry_node(entry));
    }
    SerializedNode::Array resources;
    resources.reserve(response->resources.size());
    for (const auto& entry : response->resources) {
        resources.push_back(audit_entry_node(entry));
    }
    return encode_node(
        SerializedNode::object({
            SerializedField {"ready", SerializedNode::boolean(response->ready)},
            SerializedField {
                "complete",
                SerializedNode::boolean(response->complete),
            },
            SerializedField {
                "runtime_ready",
                SerializedNode::boolean(response->runtime_ready),
            },
            SerializedField {
                "runtime_message",
                SerializedNode::string(response->runtime_message),
            },
            SerializedField {
                "entity_count",
                SerializedNode::unsigned_integer(response->entity_count),
            },
            SerializedField {
                "components",
                SerializedNode::array(std::move(components)),
            },
            SerializedField {
                "resources",
                SerializedNode::array(std::move(resources)),
            },
        })
    );
}

Result<std::string, InspectionError>
delete_checkpoint_json(World& world, std::string_view request_json) {
    auto request = parse_checkpoint_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = DeleteCheckpointProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return encode_node(
        SerializedNode::object({SerializedField {
            "name",
            SerializedNode::string(response->name),
        }})
    );
}

Result<std::string, InspectionError>
clear_checkpoints_json(World& world, std::string_view request_json) {
    if (auto status = parse_empty_request(request_json); !status) {
        return failure(std::move(status.error()));
    }
    auto response =
        ClearCheckpointsProvider {}.inspect(world, CheckpointClearRequest {});
    if (!response) {
        return failure(std::move(response.error()));
    }
    return encode_node(
        SerializedNode::object({SerializedField {
            "removed",
            SerializedNode::unsigned_integer(response->removed),
        }})
    );
}

Status<InspectionError>
register_checkpoint_inspection_providers(InspectionRegistry& registry) {
    auto status = registry.add<CreateCheckpointProvider>(
        [](World& world, std::string_view payload_json) {
            return create_checkpoint_json(world, payload_json);
        }
    );
    if (!status) {
        return status;
    }
    status = registry.add<ListCheckpointsProvider>(
        [](World& world, std::string_view payload_json) {
            return list_checkpoints_json(world, payload_json);
        }
    );
    if (!status) {
        return status;
    }
    status = registry.add<RestoreCheckpointProvider>(
        [](World& world, std::string_view payload_json) {
            return restore_checkpoint_json(world, payload_json);
        }
    );
    if (!status) {
        return status;
    }
    status = registry.add<AuditCheckpointProvider>(
        [](World& world, std::string_view payload_json) {
            return audit_checkpoint_json(world, payload_json);
        }
    );
    if (!status) {
        return status;
    }
    status = registry.add<DeleteCheckpointProvider>(
        [](World& world, std::string_view payload_json) {
            return delete_checkpoint_json(world, payload_json);
        }
    );
    if (!status) {
        return status;
    }
    return registry.add<ClearCheckpointsProvider>(
        [](World& world, std::string_view payload_json) {
            return clear_checkpoints_json(world, payload_json);
        }
    );
}

} // namespace fei::runtime_inspection::checkpoint
