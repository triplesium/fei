#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/change_detection.hpp"
#include "ecs/fwd.hpp"
#include "ecs/runtime_state.hpp"
#include "refl/type.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fei {

class World;

namespace snapshot {

struct SnapshotArchiveMetadata;

using SnapshotEntityId = std::uint64_t;

struct SnapshotComponent {
    std::string type_name;
    serialization::SerializedNode value;
    ComponentTicks ticks;
    bool derived {false};
};

struct SnapshotEntity {
    SnapshotEntityId id {};
    Entity runtime_id {};
    std::vector<SnapshotComponent> components;
};

struct SnapshotResource {
    std::string type_name;
    serialization::SerializedNode value;
    ComponentTicks ticks;
};

struct SnapshotDynamicEventChannel {
    std::string type_name;
    std::vector<serialization::SerializedNode> previous;
    std::size_t previous_start {};
    std::vector<serialization::SerializedNode> current;
    std::size_t current_start {};
    std::size_t event_count {};
};

struct WorldSnapshot {
    static constexpr std::uint32_t c_current_version = 2;

    std::uint32_t version {c_current_version};
    std::vector<SnapshotEntity> entities;
    std::vector<SnapshotResource> resources;
    bool dynamic_events_present {false};
    std::vector<SnapshotDynamicEventChannel> dynamic_events;
    WorldRuntimeState runtime;
};

struct SnapshotError {
    enum class Kind {
        UnsupportedVersion,
        InvalidSnapshot,
        InvalidConfiguration,
        TypeNotFound,
        MissingResource,
        InvalidEntityReference,
        SerializeFailed,
        DeserializeFailed,
        RestoreHookFailed,
        CheckpointBoundaryFailed,
        RuntimeStateFailed,
        StrictAuditFailed,
        InvalidCheckpointName,
        CheckpointNotFound,
        CheckpointLimitReached,
        CheckpointTooLarge,
        ArchiveIoFailed,
        ArchiveFormatFailed,
        IncompatibleArchive,
        PersistentStateUnsupported,
    };

    Kind kind {};
    std::string path;
    std::string message;
};

class ResourceRegistry {
  public:
    bool include(TypeId type);

    template<class T>
    bool include() {
        return include(type_id<T>());
    }

    [[nodiscard]] bool contains(TypeId type) const;
    bool exclude(TypeId type);
    const std::vector<TypeId>& types() const { return m_types; }

  private:
    std::vector<TypeId> m_types;
};

enum class ComponentPolicy {
    Snapshot,
    Ignore,
    Rebuild,
};

enum class ResourcePolicy {
    Snapshot,
    Ignore,
    Rebuild,
};

enum class AuditDisposition {
    Snapshot,
    Ignore,
    Rebuild,
    Derived,
    Unregistered,
};

struct SnapshotAuditEntry {
    TypeId type;
    std::string type_name;
    std::size_t instances {};
    AuditDisposition disposition {AuditDisposition::Snapshot};
    bool serializable {true};
    std::string message;
};

struct SnapshotAudit {
    bool ready {true};
    bool complete {true};
    bool runtime_ready {true};
    std::size_t entity_count {};
    std::string runtime_message;
    std::vector<SnapshotAuditEntry> components;
    std::vector<SnapshotAuditEntry> resources;
};

class SnapshotRegistry {
  public:
    using CaptureValidator = std::function<Status<SnapshotError>(const World&)>;
    using RestoreHook = std::function<Status<SnapshotError>(World&)>;

    ResourceRegistry& resources() { return m_resources; }
    const ResourceRegistry& resources() const { return m_resources; }

    bool set_component_policy(TypeId type, ComponentPolicy policy);

    template<class T>
    bool component(ComponentPolicy policy) {
        return set_component_policy(type_id<T>(), policy);
    }

    [[nodiscard]] ComponentPolicy component_policy(TypeId type) const;

    bool set_resource_policy(TypeId type, ResourcePolicy policy);

    template<class T>
    bool resource(ResourcePolicy policy) {
        return set_resource_policy(type_id<T>(), policy);
    }

    [[nodiscard]] Optional<ResourcePolicy> resource_policy(TypeId type) const;

    serialization::ValueCodecRegistry& codecs() { return m_codecs; }
    const serialization::ValueCodecRegistry& codecs() const { return m_codecs; }

    void on_validate_capture(CaptureValidator validator);
    void on_before_restore(RestoreHook hook);
    void on_after_restore(RestoreHook hook);
    void on_restore_rollback(RestoreHook hook);

    const std::vector<CaptureValidator>& capture_validators() const {
        return m_capture_validators;
    }
    const std::vector<RestoreHook>& before_restore_hooks() const {
        return m_before_restore_hooks;
    }
    const std::vector<RestoreHook>& after_restore_hooks() const {
        return m_after_restore_hooks;
    }
    const std::vector<RestoreHook>& rollback_hooks() const {
        return m_rollback_hooks;
    }

  private:
    ResourceRegistry m_resources;
    std::unordered_map<TypeId, ComponentPolicy> m_component_policies;
    std::unordered_map<TypeId, ResourcePolicy> m_resource_policies;
    serialization::ValueCodecRegistry m_codecs;
    std::vector<CaptureValidator> m_capture_validators;
    std::vector<RestoreHook> m_before_restore_hooks;
    std::vector<RestoreHook> m_after_restore_hooks;
    std::vector<RestoreHook> m_rollback_hooks;
};

[[nodiscard]] std::string_view
audit_disposition_name(AuditDisposition disposition);

[[nodiscard]] SnapshotAudit
audit(const World& world, const SnapshotRegistry& registry);

struct RestoreResult {
    std::unordered_map<SnapshotEntityId, Entity> entities;

    Entity entity(SnapshotEntityId snapshot_id) const;
};

struct CheckpointInfo {
    std::string name;
    std::uint64_t revision {};
    std::size_t entity_count {};
    std::size_t resource_count {};
    std::size_t byte_size {};
};

enum class CheckpointEviction {
    Reject,
    Oldest,
};

struct CheckpointLimits {
    std::size_t max_count {16};
    std::size_t max_bytes {64 * 1024 * 1024};
    CheckpointEviction eviction {CheckpointEviction::Oldest};
};

class CheckpointStore {
  public:
    static constexpr std::size_t c_max_checkpoints = 16;

    SnapshotRegistry& registry() { return m_registry; }
    const SnapshotRegistry& registry() const { return m_registry; }

    ResourceRegistry& resources() { return m_registry.resources(); }
    const ResourceRegistry& resources() const { return m_registry.resources(); }

    Result<CheckpointInfo, SnapshotError>
    create(const std::string& name, const World& world, bool strict = false);

    Result<RestoreResult, SnapshotError>
    restore(const std::string& name, World& world);

    [[nodiscard]] std::vector<CheckpointInfo> list() const;

    bool erase(std::string_view name);
    std::size_t clear();

    Result<CheckpointInfo, SnapshotError> export_file(
        std::string_view name,
        const std::filesystem::path& path,
        const SnapshotArchiveMetadata& metadata
    ) const;

    Result<CheckpointInfo, SnapshotError> import_file(
        std::string name,
        const std::filesystem::path& path,
        const SnapshotArchiveMetadata& expected
    );

    void set_limits(CheckpointLimits limits);
    [[nodiscard]] const CheckpointLimits& limits() const { return m_limits; }
    [[nodiscard]] std::size_t total_bytes() const { return m_total_bytes; }

  private:
    struct Entry {
        WorldSnapshot snapshot;
        std::uint64_t revision {};
        std::size_t byte_size {};
    };

    SnapshotRegistry m_registry;
    std::map<std::string, Entry, std::less<>> m_entries;
    std::uint64_t m_next_revision {1};
    CheckpointLimits m_limits;
    std::size_t m_total_bytes {};

    Result<CheckpointInfo, SnapshotError>
    insert(std::string name, WorldSnapshot snapshot);
};

Result<WorldSnapshot, SnapshotError>
capture(const World& world, const ResourceRegistry& resources = {});

Result<WorldSnapshot, SnapshotError>
capture(const World& world, const SnapshotRegistry& registry);

Result<RestoreResult, SnapshotError>
restore(World& world, const WorldSnapshot& snapshot);

Result<RestoreResult, SnapshotError> restore(
    World& world,
    const WorldSnapshot& snapshot,
    const SnapshotRegistry& registry
);

} // namespace snapshot
} // namespace fei
