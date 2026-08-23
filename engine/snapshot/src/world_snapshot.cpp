#include "snapshot/world_snapshot.hpp"

#include "ecs/archetype.hpp"
#include "ecs/dynamic/events.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"
#include "serialization/serializer.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ets::snapshot {
namespace {

using EntityToSnapshot = std::unordered_map<Entity, SnapshotEntityId>;
using SnapshotToEntity = std::unordered_map<SnapshotEntityId, Entity>;

SnapshotError
make_error(SnapshotError::Kind kind, std::string path, std::string message) {
    return SnapshotError {
        .kind = kind,
        .path = std::move(path),
        .message = std::move(message),
    };
}

std::vector<Entity> collect_entities(const World& world) {
    std::vector<Entity> entities;
    for (const auto& [_, archetype] : world.archetypes()) {
        entities.insert(
            entities.end(),
            archetype.entities().begin(),
            archetype.entities().end()
        );
    }
    std::sort(entities.begin(), entities.end());
    return entities;
}

Type& entity_reference_type() {
    return Registry::instance().register_type<Entity>();
}

Result<serialization::ValueCodecRegistry, SnapshotError> capture_codecs(
    const EntityToSnapshot& entity_ids,
    const serialization::ValueCodecRegistry& custom_codecs
) {
    entity_reference_type();
    if (custom_codecs.find(type_id<Entity>()) != nullptr) {
        return failure(make_error(
            SnapshotError::Kind::InvalidConfiguration,
            "codecs",
            "Entity uses the built-in snapshot codec and cannot be "
            "overridden"
        ));
    }
    auto codecs = custom_codecs;
    codecs.register_codec<Entity>(serialization::ValueCodec {
        .encode = [&entity_ids](Ref value, std::string_view path)
            -> Result<
                serialization::SerializedNode,
                serialization::SerializeError> {
            const auto entity = value.get_const<Entity>();
            const auto found = entity_ids.find(entity);
            if (found == entity_ids.end()) {
                return failure(
                    serialization::SerializeError {
                        .kind = serialization::SerializeError::Kind::
                            UnsupportedType,
                        .type = type_id<Entity>(),
                        .path = std::string(path),
                        .message = "Entity reference points outside the "
                                   "captured world: " +
                                   std::to_string(entity.value),
                    }
                );
            }
            return serialization::SerializedNode::unsigned_integer(
                found->second
            );
        },
        .decode = [](const serialization::SerializedNode&,
                     std::string_view path)
            -> Result<Val, serialization::DeserializeError> {
            return failure(
                serialization::DeserializeError {
                    .kind =
                        serialization::DeserializeError::Kind::UnsupportedType,
                    .type = type_id<Entity>(),
                    .path = std::string(path),
                    .message = "Capture codec cannot decode entity references",
                }
            );
        },
    });
    return codecs;
}

Result<serialization::ValueCodecRegistry, SnapshotError> restore_codecs(
    const SnapshotToEntity& entities,
    const serialization::ValueCodecRegistry& custom_codecs
) {
    auto& reference_type = entity_reference_type();
    if (custom_codecs.find(type_id<Entity>()) != nullptr) {
        return failure(make_error(
            SnapshotError::Kind::InvalidConfiguration,
            "codecs",
            "Entity uses the built-in snapshot codec and cannot be "
            "overridden"
        ));
    }
    auto codecs = custom_codecs;
    codecs.register_codec<Entity>(serialization::ValueCodec {
        .encode =
            [](Ref, std::string_view path) -> Result<
                                               serialization::SerializedNode,
                                               serialization::SerializeError> {
            return failure(
                serialization::SerializeError {
                    .kind =
                        serialization::SerializeError::Kind::UnsupportedType,
                    .type = type_id<Entity>(),
                    .path = std::string(path),
                    .message = "Restore codec cannot encode entity references",
                }
            );
        },
        .decode = [&entities, &reference_type](
                      const serialization::SerializedNode& node,
                      std::string_view path
                  ) -> Result<Val, serialization::DeserializeError> {
            const auto* snapshot_id = node.try_unsigned_integer();
            if (!snapshot_id) {
                return failure(
                    serialization::DeserializeError {
                        .kind =
                            serialization::DeserializeError::Kind::InvalidNode,
                        .type = type_id<Entity>(),
                        .path = std::string(path),
                        .message =
                            "Entity reference must be an unsigned snapshot id",
                    }
                );
            }
            const auto found = entities.find(*snapshot_id);
            if (found == entities.end()) {
                return failure(
                    serialization::DeserializeError {
                        .kind =
                            serialization::DeserializeError::Kind::InvalidNode,
                        .type = type_id<Entity>(),
                        .path = std::string(path),
                        .message =
                            "Entity reference points to unknown snapshot id " +
                            std::to_string(*snapshot_id),
                    }
                );
            }
            return Val::construct(reference_type, [&](void* destination) {
                new (destination) Entity(found->second);
            });
        },
    });
    return codecs;
}

Result<TypeId, SnapshotError>
resolve_type(std::string_view name, std::string path) {
    auto type = Registry::instance().try_get_type_exact(name);
    if (!type) {
        return failure(make_error(
            SnapshotError::Kind::TypeNotFound,
            std::move(path),
            type.error().message
        ));
    }
    return type->id();
}

SnapshotError serialization_error(const serialization::SerializeError& error) {
    auto kind = SnapshotError::Kind::SerializeFailed;
    if (error.type == type_id<Entity>()) {
        kind = SnapshotError::Kind::InvalidEntityReference;
    }
    return make_error(kind, error.path, error.message);
}

SnapshotError
deserialization_error(const serialization::DeserializeError& error) {
    auto kind = SnapshotError::Kind::DeserializeFailed;
    if (error.type == type_id<Entity>()) {
        kind = SnapshotError::Kind::InvalidEntityReference;
    }
    return make_error(kind, error.path, error.message);
}

SnapshotError runtime_state_error(RuntimeStateError error) {
    const auto kind = error.path == "commands" ?
                          SnapshotError::Kind::CheckpointBoundaryFailed :
                          SnapshotError::Kind::RuntimeStateFailed;
    return make_error(kind, std::move(error.path), std::move(error.message));
}

bool valid_checkpoint_name(std::string_view name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    return std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_' ||
               character == '-';
    });
}

std::size_t serialized_node_size(const serialization::SerializedNode& node) {
    constexpr std::size_t c_tag_size = 1;
    if (const auto* text = node.try_string()) {
        return c_tag_size + text->size();
    }
    if (const auto* array = node.try_array()) {
        std::size_t size = c_tag_size;
        for (const auto& element : *array) {
            size += serialized_node_size(element);
        }
        return size;
    }
    if (const auto* object = node.try_object()) {
        std::size_t size = c_tag_size;
        for (const auto& field : *object) {
            size += field.name.size() + serialized_node_size(field.value);
        }
        return size;
    }
    return c_tag_size + sizeof(std::uint64_t);
}

std::size_t snapshot_size(const WorldSnapshot& snapshot) {
    std::size_t size = sizeof(snapshot.version) +
                       sizeof(snapshot.runtime.change_tick) +
                       sizeof(snapshot.runtime.registered_system_generation) +
                       sizeof(snapshot.runtime.schedules.topology_generation) +
                       snapshot.runtime.removed_components.byte_size();
    for (const auto& entity : snapshot.entities) {
        size += sizeof(entity.id) + sizeof(entity.runtime_id);
        for (const auto& component : entity.components) {
            size += sizeof(component.ticks) + sizeof(component.derived) +
                    component.type_name.size() +
                    serialized_node_size(component.value);
        }
    }
    for (const auto& resource : snapshot.resources) {
        size += sizeof(resource.ticks) + resource.type_name.size() +
                serialized_node_size(resource.value);
    }
    for (const auto& channel : snapshot.dynamic_events) {
        size += channel.type_name.size() + sizeof(channel.previous_start) +
                sizeof(channel.current_start) + sizeof(channel.event_count);
        for (const auto& event : channel.previous) {
            size += serialized_node_size(event);
        }
        for (const auto& event : channel.current) {
            size += serialized_node_size(event);
        }
    }
    for (const auto& schedule : snapshot.runtime.schedules.schedules) {
        size += sizeof(schedule.id);
        for (const auto& system : schedule.systems) {
            size +=
                sizeof(system.id) + sizeof(system.system.last_run) +
                system.system.executor.byte_size() +
                system.system.params.size() * sizeof(SystemParamRuntimeState);
            for (const auto& condition : system.conditions) {
                size +=
                    sizeof(condition.last_run) +
                    condition.executor.byte_size() +
                    condition.params.size() * sizeof(SystemParamRuntimeState);
            }
        }
    }
    for (const auto& system : snapshot.runtime.registered_systems) {
        size += sizeof(system.id) + sizeof(system.system.last_run) +
                system.system.executor.byte_size() +
                system.system.params.size() * sizeof(SystemParamRuntimeState);
    }
    return size;
}

} // namespace

bool ResourceRegistry::include(TypeId type) {
    if (std::ranges::find(m_types, type) != m_types.end()) {
        return false;
    }
    m_types.push_back(type);
    return true;
}

bool ResourceRegistry::contains(TypeId type) const {
    return std::ranges::find(m_types, type) != m_types.end();
}

bool ResourceRegistry::exclude(TypeId type) {
    const auto found = std::ranges::find(m_types, type);
    if (found == m_types.end()) {
        return false;
    }
    m_types.erase(found);
    return true;
}

std::string_view audit_disposition_name(AuditDisposition disposition) {
    switch (disposition) {
        case AuditDisposition::Snapshot:
            return "snapshot";
        case AuditDisposition::Ignore:
            return "ignore";
        case AuditDisposition::Rebuild:
            return "rebuild";
        case AuditDisposition::Derived:
            return "derived";
        case AuditDisposition::Unregistered:
            return "unregistered";
    }
    return "unknown";
}

Status<SnapshotError>
validate_capture(const World& world, const SnapshotRegistry& registry) {
    for (const auto& validator : registry.capture_validators()) {
        try {
            auto status = validator(world);
            if (!status) {
                return status;
            }
        } catch (const std::exception& error) {
            return failure(make_error(
                SnapshotError::Kind::CheckpointBoundaryFailed,
                "capture",
                error.what()
            ));
        } catch (...) {
            return failure(make_error(
                SnapshotError::Kind::CheckpointBoundaryFailed,
                "capture",
                "Capture validator threw an unknown exception"
            ));
        }
    }
    return {};
}

SnapshotAudit audit(const World& world, const SnapshotRegistry& registry) {
    SnapshotAudit result;
    auto boundary = validate_capture(world, registry);
    if (!boundary) {
        result.ready = false;
        result.runtime_ready = false;
        result.runtime_message =
            boundary.error().path.empty() ?
                boundary.error().message :
                boundary.error().path + ": " + boundary.error().message;
    }
    auto runtime = world.capture_runtime_state();
    if (!runtime) {
        result.ready = false;
        result.runtime_ready = false;
        result.runtime_message =
            runtime.error().path.empty() ?
                runtime.error().message :
                runtime.error().path + ": " + runtime.error().message;
    }
    const auto entities = collect_entities(world);
    result.entity_count = entities.size();

    EntityToSnapshot entity_ids;
    for (std::size_t index = 0; index < entities.size(); ++index) {
        entity_ids.emplace(
            entities[index],
            static_cast<SnapshotEntityId>(index + 1)
        );
    }
    auto codecs = capture_codecs(entity_ids, registry.codecs());
    if (!codecs) {
        result.ready = false;
    }
    const serialization::SerializeOptions options {
        .include_type_tag = false,
        .codecs = codecs ? &*codecs : nullptr,
    };

    std::unordered_map<TypeId, std::size_t> component_entries;
    for (const auto entity : entities) {
        const auto location = world.entity_location(entity);
        if (!location) {
            continue;
        }
        const auto& archetype = world.archetypes().get(location->archetype_id);
        for (const auto component_type : archetype.components()) {
            auto [found, inserted] = component_entries.try_emplace(
                component_type,
                result.components.size()
            );
            if (inserted) {
                auto reflected =
                    Registry::instance().try_get_type(component_type);
                auto disposition = AuditDisposition::Snapshot;
                if (component_type == type_id<Children>()) {
                    disposition = AuditDisposition::Derived;
                } else {
                    switch (registry.component_policy(component_type)) {
                        case ComponentPolicy::Snapshot:
                            break;
                        case ComponentPolicy::Ignore:
                            disposition = AuditDisposition::Ignore;
                            break;
                        case ComponentPolicy::Rebuild:
                            disposition = AuditDisposition::Rebuild;
                            break;
                    }
                }
                result.components.push_back(
                    SnapshotAuditEntry {
                        .type = component_type,
                        .type_name =
                            reflected ?
                                reflected->name() :
                                "<type:" + std::to_string(component_type.id()) +
                                    ">",
                        .disposition = disposition,
                        .serializable = reflected.has_value() && codecs,
                        .message = reflected ? std::string {} :
                                               "Type is not reflected",
                    }
                );
            }
            auto& entry = result.components[found->second];
            ++entry.instances;
            if (entry.disposition != AuditDisposition::Snapshot ||
                !entry.serializable) {
                if (entry.disposition == AuditDisposition::Snapshot &&
                    !entry.serializable) {
                    result.ready = false;
                }
                continue;
            }

            if (component_type == type_id<ChildOf>()) {
                const auto parent =
                    archetype.get_component(component_type, location->row)
                        .get_const<ChildOf>()
                        .parent;
                if (!entity_ids.contains(parent)) {
                    entry.serializable = false;
                    entry.message = "ChildOf points outside the captured world";
                    result.ready = false;
                }
                continue;
            }
            auto serialized = serialization::serialize(
                archetype.get_component(component_type, location->row),
                options
            );
            if (!serialized) {
                entry.serializable = false;
                entry.message = serialized.error().message;
                result.ready = false;
            }
        }
    }

    for (const auto resource_type : world.resource_types()) {
        auto reflected = Registry::instance().try_get_type(resource_type);
        const auto policy = registry.resource_policy(resource_type);
        auto disposition = AuditDisposition::Unregistered;
        if (policy) {
            switch (*policy) {
                case ResourcePolicy::Snapshot:
                    disposition = AuditDisposition::Snapshot;
                    break;
                case ResourcePolicy::Ignore:
                    disposition = AuditDisposition::Ignore;
                    break;
                case ResourcePolicy::Rebuild:
                    disposition = AuditDisposition::Rebuild;
                    break;
            }
        } else {
            result.complete = false;
        }
        SnapshotAuditEntry entry {
            .type = resource_type,
            .type_name =
                reflected ? reflected->name() :
                            "<type:" + std::to_string(resource_type.id()) + ">",
            .instances = 1,
            .disposition = disposition,
            .serializable = disposition != AuditDisposition::Snapshot,
            .message =
                policy ? std::string {} : "Resource has no snapshot policy",
        };
        if (disposition == AuditDisposition::Snapshot &&
            resource_type == type_id<DynamicEvents>()) {
            entry.serializable = codecs.has_value();
            if (!codecs) {
                entry.message = codecs.error().message;
            } else {
                const auto& dynamic_events = world.resource<DynamicEvents>();
                for (const auto& [event_type, channel] :
                     dynamic_events.channels()) {
                    auto event_reflection =
                        Registry::instance().try_get_type(event_type);
                    if (!event_reflection) {
                        entry.serializable = false;
                        entry.message = event_reflection.error().message;
                        break;
                    }
                    const auto audit_sequence = [&](const auto& events) {
                        for (const auto& event : events) {
                            auto serialized =
                                serialization::serialize(event.ref(), options);
                            if (!serialized) {
                                entry.serializable = false;
                                entry.message =
                                    "Dynamic event '" +
                                    event_reflection->name() +
                                    "': " + serialized.error().message;
                                return false;
                            }
                        }
                        return true;
                    };
                    if (!audit_sequence(channel.previous.events) ||
                        !audit_sequence(channel.current.events)) {
                        break;
                    }
                }
            }
        } else if (
            disposition == AuditDisposition::Snapshot && (!reflected || !codecs)
        ) {
            entry.serializable = false;
            entry.message =
                reflected ? codecs.error().message : "Type is not reflected";
        } else if (disposition == AuditDisposition::Snapshot) {
            auto serialized = serialization::serialize(
                world.resource(resource_type),
                options
            );
            entry.serializable = serialized.has_value();
            if (!serialized) {
                entry.message = serialized.error().message;
            }
        }
        if (disposition == AuditDisposition::Snapshot && !entry.serializable) {
            result.ready = false;
        }
        result.resources.push_back(std::move(entry));
    }
    for (const auto resource_type : registry.resources().types()) {
        if (world.has_resource(resource_type)) {
            continue;
        }
        auto reflected = Registry::instance().try_get_type(resource_type);
        result.resources.push_back(
            SnapshotAuditEntry {
                .type = resource_type,
                .type_name =
                    reflected ?
                        reflected->name() :
                        "<type:" + std::to_string(resource_type.id()) + ">",
                .instances = 0,
                .disposition = AuditDisposition::Snapshot,
                .serializable = false,
                .message = "Registered resource is missing from the world",
            }
        );
        result.ready = false;
    }
    auto by_name = [](const SnapshotAuditEntry& left,
                      const SnapshotAuditEntry& right) {
        return left.type_name < right.type_name;
    };
    std::ranges::sort(result.components, by_name);
    std::ranges::sort(result.resources, by_name);
    return result;
}

bool SnapshotRegistry::set_component_policy(
    TypeId type,
    ComponentPolicy policy
) {
    const auto [entry, inserted] =
        m_component_policies.insert_or_assign(type, policy);
    (void)entry;
    return inserted;
}

ComponentPolicy SnapshotRegistry::component_policy(TypeId type) const {
    const auto policy = m_component_policies.find(type);
    return policy == m_component_policies.end() ? ComponentPolicy::Snapshot :
                                                  policy->second;
}

bool SnapshotRegistry::set_resource_policy(TypeId type, ResourcePolicy policy) {
    if (policy == ResourcePolicy::Snapshot) {
        m_resources.include(type);
    } else {
        m_resources.exclude(type);
    }
    const auto [_, inserted] =
        m_resource_policies.insert_or_assign(type, policy);
    return inserted;
}

Optional<ResourcePolicy> SnapshotRegistry::resource_policy(TypeId type) const {
    if (const auto policy = m_resource_policies.find(type);
        policy != m_resource_policies.end()) {
        return policy->second;
    }
    if (m_resources.contains(type)) {
        return ResourcePolicy::Snapshot;
    }
    return nullopt;
}

void SnapshotRegistry::on_validate_capture(CaptureValidator validator) {
    if (validator) {
        m_capture_validators.push_back(std::move(validator));
    }
}

void SnapshotRegistry::on_before_restore(RestoreHook hook) {
    if (hook) {
        m_before_restore_hooks.push_back(std::move(hook));
    }
}

void SnapshotRegistry::on_after_restore(RestoreHook hook) {
    if (hook) {
        m_after_restore_hooks.push_back(std::move(hook));
    }
}

void SnapshotRegistry::on_restore_rollback(RestoreHook hook) {
    if (hook) {
        m_rollback_hooks.push_back(std::move(hook));
    }
}

Entity RestoreResult::entity(SnapshotEntityId snapshot_id) const {
    const auto found = entities.find(snapshot_id);
    return found == entities.end() ? Entity {} : found->second;
}

Result<CheckpointInfo, SnapshotError> CheckpointStore::create(
    const std::string& name,
    const World& world,
    bool strict
) {
    if (!valid_checkpoint_name(name)) {
        return failure(make_error(
            SnapshotError::Kind::InvalidCheckpointName,
            "name",
            "Checkpoint name must contain 1-64 ASCII letters, digits, '_' or "
            "'-'"
        ));
    }
    if (strict) {
        const auto coverage = snapshot::audit(world, m_registry);
        if (!coverage.ready || !coverage.complete) {
            return failure(make_error(
                SnapshotError::Kind::StrictAuditFailed,
                "audit",
                !coverage.ready ?
                    (coverage.runtime_ready ?
                         "Snapshot contains state that cannot be serialized" :
                         coverage.runtime_message) :
                    "Snapshot contains resources without an explicit policy"
            ));
        }
    }
    auto captured = capture(world, m_registry);
    if (!captured) {
        return failure(std::move(captured.error()));
    }
    return insert(name, std::move(*captured));
}

Result<CheckpointInfo, SnapshotError>
CheckpointStore::insert(std::string name, WorldSnapshot snapshot) {
    if (!valid_checkpoint_name(name)) {
        return failure(make_error(
            SnapshotError::Kind::InvalidCheckpointName,
            "name",
            "Checkpoint name must contain 1-64 ASCII letters, digits, '_' or "
            "'-'"
        ));
    }

    const auto byte_size = snapshot_size(snapshot);
    if (byte_size > m_limits.max_bytes) {
        return failure(make_error(
            SnapshotError::Kind::CheckpointTooLarge,
            "name",
            "Checkpoint exceeds the configured memory budget"
        ));
    }

    const auto existing = m_entries.find(name);
    const auto existing_bytes =
        existing == m_entries.end() ? 0 : existing->second.byte_size;
    auto exceeds_limits = [&] {
        const auto resulting_count =
            m_entries.size() + (existing == m_entries.end() ? 1 : 0);
        const auto resulting_bytes = m_total_bytes - existing_bytes + byte_size;
        return resulting_count > m_limits.max_count ||
               resulting_bytes > m_limits.max_bytes;
    };
    while (exceeds_limits()) {
        if (m_limits.eviction == CheckpointEviction::Reject) {
            return failure(make_error(
                SnapshotError::Kind::CheckpointLimitReached,
                "name",
                "Checkpoint store count or memory limit would be exceeded"
            ));
        }
        auto oldest = m_entries.end();
        for (auto entry = m_entries.begin(); entry != m_entries.end();
             ++entry) {
            if (entry->first == name) {
                continue;
            }
            if (oldest == m_entries.end() ||
                entry->second.revision < oldest->second.revision) {
                oldest = entry;
            }
        }
        if (oldest == m_entries.end()) {
            return failure(make_error(
                SnapshotError::Kind::CheckpointLimitReached,
                "name",
                "Checkpoint limits cannot retain this entry"
            ));
        }
        m_total_bytes -= oldest->second.byte_size;
        m_entries.erase(oldest);
    }

    const auto revision = m_next_revision++;
    const auto entity_count = snapshot.entities.size();
    const auto resource_count = snapshot.resources.size();
    if (existing != m_entries.end()) {
        m_total_bytes -= existing->second.byte_size;
    }
    m_entries.insert_or_assign(
        name,
        Entry {
            .snapshot = std::move(snapshot),
            .revision = revision,
            .byte_size = byte_size,
        }
    );
    m_total_bytes += byte_size;
    return CheckpointInfo {
        .name = name,
        .revision = revision,
        .entity_count = entity_count,
        .resource_count = resource_count,
        .byte_size = byte_size,
    };
}

Result<RestoreResult, SnapshotError>
CheckpointStore::restore(const std::string& name, World& world) {
    if (!valid_checkpoint_name(name)) {
        return failure(make_error(
            SnapshotError::Kind::InvalidCheckpointName,
            "name",
            "Checkpoint name must contain 1-64 ASCII letters, digits, '_' or "
            "'-'"
        ));
    }
    const auto entry = m_entries.find(name);
    if (entry == m_entries.end()) {
        return failure(make_error(
            SnapshotError::Kind::CheckpointNotFound,
            "name",
            "Checkpoint '" + name + "' does not exist"
        ));
    }
    return snapshot::restore(world, entry->second.snapshot, m_registry);
}

std::vector<CheckpointInfo> CheckpointStore::list() const {
    std::vector<CheckpointInfo> checkpoints;
    checkpoints.reserve(m_entries.size());
    for (const auto& [name, entry] : m_entries) {
        checkpoints.push_back(
            CheckpointInfo {
                .name = name,
                .revision = entry.revision,
                .entity_count = entry.snapshot.entities.size(),
                .resource_count =
                    entry.snapshot.resources.size() +
                    (entry.snapshot.dynamic_events_present ? 1 : 0),
                .byte_size = entry.byte_size,
            }
        );
    }
    std::ranges::sort(checkpoints, {}, &CheckpointInfo::revision);
    return checkpoints;
}

bool CheckpointStore::erase(std::string_view name) {
    const auto entry = m_entries.find(name);
    if (entry == m_entries.end()) {
        return false;
    }
    m_total_bytes -= entry->second.byte_size;
    m_entries.erase(entry);
    return true;
}

std::size_t CheckpointStore::clear() {
    const auto count = m_entries.size();
    m_entries.clear();
    m_total_bytes = 0;
    return count;
}

void CheckpointStore::set_limits(CheckpointLimits limits) {
    m_limits = limits;
    if (m_limits.eviction != CheckpointEviction::Oldest) {
        return;
    }
    while (!m_entries.empty() && (m_entries.size() > m_limits.max_count ||
                                  m_total_bytes > m_limits.max_bytes)) {
        auto oldest =
            std::ranges::min_element(m_entries, {}, [](const auto& entry) {
                return entry.second.revision;
            });
        m_total_bytes -= oldest->second.byte_size;
        m_entries.erase(oldest);
    }
}

Result<WorldSnapshot, SnapshotError>
capture(const World& world, const ResourceRegistry& resources) {
    SnapshotRegistry registry;
    for (const auto type : resources.types()) {
        registry.resources().include(type);
    }
    return capture(world, registry);
}

Result<WorldSnapshot, SnapshotError>
capture(const World& world, const SnapshotRegistry& registry) {
    WorldSnapshot snapshot;
    auto boundary = validate_capture(world, registry);
    if (!boundary) {
        return failure(std::move(boundary.error()));
    }
    auto runtime = world.capture_runtime_state();
    if (!runtime) {
        return failure(runtime_state_error(std::move(runtime.error())));
    }
    snapshot.runtime = std::move(*runtime);
    const auto world_entities = collect_entities(world);

    EntityToSnapshot entity_ids;
    for (std::size_t index = 0; index < world_entities.size(); ++index) {
        entity_ids.emplace(
            world_entities[index],
            static_cast<SnapshotEntityId>(index + 1)
        );
    }
    auto codecs = capture_codecs(entity_ids, registry.codecs());
    if (!codecs) {
        return failure(std::move(codecs.error()));
    }
    const serialization::SerializeOptions options {
        .include_type_tag = false,
        .codecs = &*codecs,
    };

    snapshot.entities.reserve(world_entities.size());
    for (const auto entity : world_entities) {
        const auto location = world.entity_location(entity);
        if (!location) {
            continue;
        }
        const auto& archetype = world.archetypes().get(location->archetype_id);
        SnapshotEntity snapshot_entity {
            .id = entity_ids.at(entity),
            .runtime_id = entity,
        };
        snapshot_entity.components.reserve(archetype.components().size());
        for (const auto component_type : archetype.components()) {
            const auto derived = component_type == type_id<Children>();
            if (!derived && registry.component_policy(component_type) !=
                                ComponentPolicy::Snapshot) {
                continue;
            }
            auto reflected_type =
                Registry::instance().try_get_type(component_type);
            if (!reflected_type) {
                return failure(make_error(
                    SnapshotError::Kind::TypeNotFound,
                    "entities[" + std::to_string(snapshot_entity.id) + "]",
                    reflected_type.error().message
                ));
            }
            Result<serialization::SerializedNode, serialization::SerializeError>
                node;
            if (derived) {
                node = serialization::SerializedNode::null();
            } else if (component_type == type_id<ChildOf>()) {
                const auto parent =
                    archetype.get_component(component_type, location->row)
                        .get_const<ChildOf>()
                        .parent;
                const auto parent_id = entity_ids.find(parent);
                if (parent_id == entity_ids.end()) {
                    return failure(make_error(
                        SnapshotError::Kind::InvalidEntityReference,
                        "entities[" + std::to_string(snapshot_entity.id) +
                            "]." + reflected_type->name(),
                        "ChildOf points outside the captured world: " +
                            std::to_string(parent.value)
                    ));
                }
                node = serialization::SerializedNode::unsigned_integer(
                    parent_id->second
                );
            } else {
                node = serialization::serialize(
                    archetype.get_component(component_type, location->row),
                    options
                );
            }
            if (!node) {
                return failure(serialization_error(node.error()));
            }
            snapshot_entity.components.push_back(
                SnapshotComponent {
                    .type_name = reflected_type->name(),
                    .value = std::move(*node),
                    .ticks = archetype.component_ticks(
                        component_type,
                        location->row
                    ),
                    .derived = derived,
                }
            );
        }
        std::ranges::sort(
            snapshot_entity.components,
            {},
            &SnapshotComponent::type_name
        );
        snapshot.entities.push_back(std::move(snapshot_entity));
    }

    const bool snapshot_dynamic_events =
        registry.resources().contains(type_id<DynamicEvents>());
    if (snapshot_dynamic_events && !world.has_resource<DynamicEvents>()) {
        return failure(make_error(
            SnapshotError::Kind::MissingResource,
            "resources.DynamicEvents",
            "Registered snapshot resource is missing from the world"
        ));
    }

    std::vector<std::pair<std::string, TypeId>> resource_types;
    resource_types.reserve(registry.resources().types().size());
    for (const auto resource_type : registry.resources().types()) {
        if (resource_type == type_id<DynamicEvents>()) {
            continue;
        }
        auto reflected_type = Registry::instance().try_get_type(resource_type);
        if (!reflected_type) {
            return failure(make_error(
                SnapshotError::Kind::TypeNotFound,
                "resources",
                reflected_type.error().message
            ));
        }
        if (!world.has_resource(resource_type)) {
            return failure(make_error(
                SnapshotError::Kind::MissingResource,
                "resources." + reflected_type->name(),
                "Registered snapshot resource is missing from the world"
            ));
        }
        resource_types.emplace_back(reflected_type->name(), resource_type);
    }
    std::ranges::sort(resource_types);

    snapshot.resources.reserve(resource_types.size());
    for (const auto& [type_name, type] : resource_types) {
        auto node = serialization::serialize(world.resource(type), options);
        if (!node) {
            return failure(serialization_error(node.error()));
        }
        snapshot.resources.push_back(
            SnapshotResource {
                .type_name = type_name,
                .value = std::move(*node),
                .ticks = world.resource_ticks(type),
            }
        );
    }
    snapshot.dynamic_events_present = snapshot_dynamic_events;
    if (snapshot.dynamic_events_present) {
        const auto& events = world.resource<DynamicEvents>();
        std::vector<std::pair<std::string, TypeId>> event_types;
        event_types.reserve(events.channels().size());
        for (const auto& [type, _] : events.channels()) {
            auto reflected = Registry::instance().try_get_type(type);
            if (!reflected) {
                return failure(make_error(
                    SnapshotError::Kind::TypeNotFound,
                    "dynamic_events",
                    reflected.error().message
                ));
            }
            event_types.emplace_back(reflected->name(), type);
        }
        std::ranges::sort(event_types);
        snapshot.dynamic_events.reserve(event_types.size());
        for (const auto& [type_name, type] : event_types) {
            const auto& channel = *events.channel(type);
            SnapshotDynamicEventChannel encoded {
                .type_name = type_name,
                .previous_start = channel.previous.start_event_count,
                .current_start = channel.current.start_event_count,
                .event_count = channel.event_count,
            };
            encoded.previous.reserve(channel.previous.events.size());
            for (const auto& event : channel.previous.events) {
                auto node = serialization::serialize(event.ref(), options);
                if (!node) {
                    return failure(serialization_error(node.error()));
                }
                encoded.previous.push_back(std::move(*node));
            }
            encoded.current.reserve(channel.current.events.size());
            for (const auto& event : channel.current.events) {
                auto node = serialization::serialize(event.ref(), options);
                if (!node) {
                    return failure(serialization_error(node.error()));
                }
                encoded.current.push_back(std::move(*node));
            }
            snapshot.dynamic_events.push_back(std::move(encoded));
        }
    }
    return snapshot;
}

Result<RestoreResult, SnapshotError>
restore(World& world, const WorldSnapshot& snapshot) {
    const SnapshotRegistry registry;
    return restore(world, snapshot, registry);
}

Result<RestoreResult, SnapshotError> restore(
    World& world,
    const WorldSnapshot& snapshot,
    const SnapshotRegistry& registry
) {
    if (snapshot.version != WorldSnapshot::c_current_version) {
        return failure(make_error(
            SnapshotError::Kind::UnsupportedVersion,
            "version",
            "Unsupported world snapshot version " +
                std::to_string(snapshot.version)
        ));
    }
    auto runtime_valid = world.validate_runtime_state(snapshot.runtime);
    if (!runtime_valid) {
        return failure(runtime_state_error(std::move(runtime_valid.error())));
    }

    std::unordered_set<SnapshotEntityId> snapshot_ids;
    std::unordered_set<Entity> runtime_entity_ids;
    std::vector<std::vector<TypeId>> component_types;
    component_types.reserve(snapshot.entities.size());
    for (std::size_t entity_index = 0; entity_index < snapshot.entities.size();
         ++entity_index) {
        const auto& snapshot_entity = snapshot.entities[entity_index];
        const auto entity_path =
            "entities[" + std::to_string(entity_index) + "]";
        if (snapshot_entity.id == 0 ||
            !snapshot_ids.insert(snapshot_entity.id).second) {
            return failure(make_error(
                SnapshotError::Kind::InvalidSnapshot,
                entity_path + ".id",
                "Snapshot entity ids must be non-zero and unique"
            ));
        }
        if (!runtime_entity_ids.insert(snapshot_entity.runtime_id).second) {
            return failure(make_error(
                SnapshotError::Kind::InvalidSnapshot,
                entity_path + ".runtime_id",
                "Snapshot runtime entity ids must be unique"
            ));
        }
        std::unordered_set<TypeId> seen_types;
        std::vector<TypeId> resolved;
        resolved.reserve(snapshot_entity.components.size());
        for (std::size_t component_index = 0;
             component_index < snapshot_entity.components.size();
             ++component_index) {
            const auto path = entity_path + ".components[" +
                              std::to_string(component_index) + "]";
            auto type = resolve_type(
                snapshot_entity.components[component_index].type_name,
                path + ".type_name"
            );
            if (!type) {
                return failure(type.error());
            }
            if (!seen_types.insert(*type).second) {
                return failure(make_error(
                    SnapshotError::Kind::InvalidSnapshot,
                    path,
                    "Entity contains a duplicate component type"
                ));
            }
            const auto derived =
                snapshot_entity.components[component_index].derived;
            if (derived && *type != type_id<Children>()) {
                return failure(make_error(
                    SnapshotError::Kind::InvalidSnapshot,
                    path,
                    "Only Children may be stored as derived component metadata"
                ));
            }
            if (!derived && *type == type_id<Children>()) {
                return failure(make_error(
                    SnapshotError::Kind::InvalidSnapshot,
                    path,
                    "Children must be stored as derived component metadata"
                ));
            }
            resolved.push_back(
                derived || registry.component_policy(*type) ==
                               ComponentPolicy::Snapshot ?
                    *type :
                    TypeId {}
            );
        }
        component_types.push_back(std::move(resolved));
    }

    std::unordered_set<TypeId> seen_resources;
    std::vector<TypeId> resource_types;
    resource_types.reserve(snapshot.resources.size());
    for (std::size_t index = 0; index < snapshot.resources.size(); ++index) {
        auto type = resolve_type(
            snapshot.resources[index].type_name,
            "resources[" + std::to_string(index) + "].type_name"
        );
        if (!type) {
            return failure(type.error());
        }
        if (!seen_resources.insert(*type).second) {
            return failure(make_error(
                SnapshotError::Kind::InvalidSnapshot,
                "resources[" + std::to_string(index) + "]",
                "Snapshot contains a duplicate resource type"
            ));
        }
        resource_types.push_back(*type);
    }

    RestoreResult restored;
    restored.entities.reserve(snapshot.entities.size());
    std::unordered_map<Entity, Entity> runtime_entities;
    runtime_entities.reserve(snapshot.entities.size());
    std::vector<Entity> new_entities;
    new_entities.reserve(snapshot.entities.size());
    for (const auto& snapshot_entity : snapshot.entities) {
        const auto entity = world.entity();
        restored.entities.emplace(snapshot_entity.id, entity);
        runtime_entities.emplace(snapshot_entity.runtime_id, entity);
        new_entities.push_back(entity);
    }
    auto restored_runtime = snapshot.runtime;
    restored_runtime.removed_components.remap_entities(runtime_entities);

    struct DecodedComponent {
        Entity entity;
        Val value;
        ComponentTicks ticks;
    };
    struct DecodedResource {
        TypeId type;
        Val value;
        ComponentTicks ticks;
    };
    struct PendingParent {
        Entity child;
        Entity parent;
        ComponentTicks ticks;
    };
    struct DerivedComponentTicks {
        Entity entity;
        TypeId type;
        ComponentTicks ticks;
    };
    std::vector<DecodedComponent> decoded_components;
    std::vector<DecodedResource> decoded_resources;
    std::optional<DynamicEvents> decoded_dynamic_events;
    std::vector<PendingParent> pending_parents;
    std::vector<DerivedComponentTicks> derived_component_ticks;
    auto codecs = restore_codecs(restored.entities, registry.codecs());
    if (!codecs) {
        for (const auto entity : new_entities) {
            world.despawn(entity);
        }
        return failure(std::move(codecs.error()));
    }
    const serialization::DeserializeOptions options {
        .object_fields = serialization::ObjectFieldPolicy::Strict,
        .enum_input = serialization::EnumInputPolicy::NameOrInteger,
        .allow_type_tag = false,
        .codecs = &*codecs,
    };

    auto rollback_new_entities = [&] {
        for (const auto entity : new_entities) {
            if (world.has_entity(entity)) {
                world.despawn(entity);
            }
        }
    };

    for (std::size_t entity_index = 0; entity_index < snapshot.entities.size();
         ++entity_index) {
        const auto& snapshot_entity = snapshot.entities[entity_index];
        const auto target = restored.entities.at(snapshot_entity.id);
        for (std::size_t component_index = 0;
             component_index < snapshot_entity.components.size();
             ++component_index) {
            const auto component_type =
                component_types[entity_index][component_index];
            if (!component_type) {
                continue;
            }
            if (snapshot_entity.components[component_index].derived) {
                derived_component_ticks.push_back(
                    DerivedComponentTicks {
                        .entity = target,
                        .type = component_type,
                        .ticks =
                            snapshot_entity.components[component_index].ticks,
                    }
                );
                continue;
            }
            if (component_type == type_id<ChildOf>()) {
                const auto* parent_id =
                    snapshot_entity.components[component_index]
                        .value.try_unsigned_integer();
                const auto parent = parent_id == nullptr ?
                                        restored.entities.end() :
                                        restored.entities.find(*parent_id);
                if (parent == restored.entities.end()) {
                    rollback_new_entities();
                    return failure(make_error(
                        SnapshotError::Kind::InvalidEntityReference,
                        "entities[" + std::to_string(entity_index) +
                            "].components[" + std::to_string(component_index) +
                            "]",
                        "ChildOf points to an unknown snapshot entity"
                    ));
                }
                pending_parents.push_back(
                    PendingParent {
                        .child = target,
                        .parent = parent->second,
                        .ticks =
                            snapshot_entity.components[component_index].ticks,
                    }
                );
                continue;
            }
            auto value = serialization::deserialize(
                component_type,
                snapshot_entity.components[component_index].value,
                options
            );
            if (!value) {
                rollback_new_entities();
                return failure(deserialization_error(value.error()));
            }
            decoded_components.push_back(
                DecodedComponent {
                    .entity = target,
                    .value = std::move(*value),
                    .ticks = snapshot_entity.components[component_index].ticks,
                }
            );
        }
    }
    for (std::size_t index = 0; index < snapshot.resources.size(); ++index) {
        auto value = serialization::deserialize(
            resource_types[index],
            snapshot.resources[index].value,
            options
        );
        if (!value) {
            rollback_new_entities();
            return failure(deserialization_error(value.error()));
        }
        decoded_resources.push_back(
            DecodedResource {
                .type = resource_types[index],
                .value = std::move(*value),
                .ticks = snapshot.resources[index].ticks,
            }
        );
    }
    if (snapshot.dynamic_events_present) {
        decoded_dynamic_events.emplace();
        std::unordered_set<TypeId> seen_event_types;
        for (std::size_t channel_index = 0;
             channel_index < snapshot.dynamic_events.size();
             ++channel_index) {
            const auto& encoded = snapshot.dynamic_events[channel_index];
            auto event_type = resolve_type(
                encoded.type_name,
                "dynamic_events[" + std::to_string(channel_index) + "]"
            );
            if (!event_type) {
                rollback_new_entities();
                return failure(std::move(event_type.error()));
            }
            if (!seen_event_types.insert(*event_type).second) {
                rollback_new_entities();
                return failure(make_error(
                    SnapshotError::Kind::InvalidSnapshot,
                    "dynamic_events[" + std::to_string(channel_index) + "]",
                    "Snapshot contains a duplicate dynamic event type"
                ));
            }
            if (encoded.previous_start + encoded.previous.size() !=
                    encoded.current_start ||
                encoded.current_start + encoded.current.size() !=
                    encoded.event_count) {
                rollback_new_entities();
                return failure(make_error(
                    SnapshotError::Kind::InvalidSnapshot,
                    "dynamic_events[" + std::to_string(channel_index) + "]",
                    "Dynamic event sequence counts are inconsistent"
                ));
            }
            DynamicEvents::Channel channel {
                .previous =
                    {
                        .start_event_count = encoded.previous_start,
                    },
                .current =
                    {
                        .start_event_count = encoded.current_start,
                    },
                .event_count = encoded.event_count,
            };
            channel.previous.events.reserve(encoded.previous.size());
            for (const auto& node : encoded.previous) {
                auto value =
                    serialization::deserialize(*event_type, node, options);
                if (!value) {
                    rollback_new_entities();
                    return failure(deserialization_error(value.error()));
                }
                channel.previous.events.push_back(std::move(*value));
            }
            channel.current.events.reserve(encoded.current.size());
            for (const auto& node : encoded.current) {
                auto value =
                    serialization::deserialize(*event_type, node, options);
                if (!value) {
                    rollback_new_entities();
                    return failure(deserialization_error(value.error()));
                }
                channel.current.events.push_back(std::move(*value));
            }
            decoded_dynamic_events->set_channel(
                *event_type,
                std::move(channel)
            );
        }
    }

    std::unordered_map<Entity, Entity> parent_by_child;
    for (const auto& relationship : pending_parents) {
        parent_by_child.emplace(relationship.child, relationship.parent);
    }
    for (const auto& relationship : pending_parents) {
        std::unordered_set<Entity> ancestry;
        auto current = relationship.child;
        while (true) {
            const auto parent = parent_by_child.find(current);
            if (parent == parent_by_child.end()) {
                break;
            }
            if (!ancestry.insert(current).second) {
                rollback_new_entities();
                return failure(make_error(
                    SnapshotError::Kind::InvalidSnapshot,
                    "entities",
                    "ChildOf relationships contain a cycle"
                ));
            }
            current = parent->second;
        }
    }

    for (const auto& hook : registry.before_restore_hooks()) {
        auto status = hook(world);
        if (!status) {
            rollback_new_entities();
            return failure(make_error(
                SnapshotError::Kind::RestoreHookFailed,
                "before_restore",
                status.error().message
            ));
        }
    }
    auto previous_runtime = world.capture_runtime_state();
    if (!previous_runtime) {
        rollback_new_entities();
        return failure(
            runtime_state_error(std::move(previous_runtime.error()))
        );
    }

    // The placeholder ids were allocated while the old graph was still live,
    // so restored entities cannot alias the previous graph. Move the complete
    // old graph and every replaced resource aside before committing.
    for (const auto entity : new_entities) {
        world.despawn(entity);
    }
    World previous;
    world.swap_entity_state(previous);
    for (const auto type : resource_types) {
        world.swap_resource(type, previous);
    }
    if (snapshot.dynamic_events_present) {
        world.swap_resource(type_id<DynamicEvents>(), previous);
    }
    for (const auto entity : new_entities) {
        world.materialize_entity(entity);
    }

    for (auto& component : decoded_components) {
        world.add_component(component.entity, component.value.ref());
        const auto location = world.entity_location(component.entity);
        world.archetypes()
            .get(location->archetype_id)
            .component_ticks(component.value.type_id(), location->row) =
            component.ticks;
    }
    for (auto& resource : decoded_resources) {
        const auto type = resource.type;
        world.add_resource(type, std::move(resource.value));
        world.resource_ticks(type) = resource.ticks;
    }
    if (decoded_dynamic_events) {
        world.add_resource(std::move(*decoded_dynamic_events));
    }
    for (const auto& relationship : pending_parents) {
        world.set_parent(relationship.child, relationship.parent);
        const auto location = world.entity_location(relationship.child);
        world.archetypes()
            .get(location->archetype_id)
            .component_ticks(type_id<ChildOf>(), location->row) =
            relationship.ticks;
    }
    for (const auto& derived : derived_component_ticks) {
        if (!world.has_component(derived.entity, derived.type)) {
            auto rollback_status =
                world.restore_runtime_state(*previous_runtime);
            for (const auto type : resource_types) {
                world.swap_resource(type, previous);
            }
            if (snapshot.dynamic_events_present) {
                world.swap_resource(type_id<DynamicEvents>(), previous);
            }
            world.swap_entity_state(previous);
            std::string message =
                "Derived component was not rebuilt from snapshot relations";
            if (!rollback_status) {
                message += "; runtime rollback failed: " +
                           rollback_status.error().message;
            }
            return failure(make_error(
                SnapshotError::Kind::InvalidSnapshot,
                "entities",
                std::move(message)
            ));
        }
        const auto location = world.entity_location(derived.entity);
        world.archetypes()
            .get(location->archetype_id)
            .component_ticks(derived.type, location->row) = derived.ticks;
    }
    auto rollback_commit = [&]() -> Status<RuntimeStateError> {
        for (const auto type : resource_types) {
            world.swap_resource(type, previous);
        }
        if (snapshot.dynamic_events_present) {
            world.swap_resource(type_id<DynamicEvents>(), previous);
        }
        world.swap_entity_state(previous);
        return world.restore_runtime_state(*previous_runtime);
    };
    auto runtime_restored = world.restore_runtime_state(restored_runtime);
    if (!runtime_restored) {
        auto error = std::move(runtime_restored.error());
        const auto rollback_status = rollback_commit();
        if (!rollback_status) {
            error.message +=
                "; runtime rollback failed: " + rollback_status.error().message;
        }
        return failure(runtime_state_error(std::move(error)));
    }
    for (const auto& hook : registry.after_restore_hooks()) {
        Optional<SnapshotError> hook_error;
        try {
            auto status = hook(world);
            if (!status) {
                hook_error = std::move(status.error());
            }
        } catch (const std::exception& error) {
            hook_error = make_error(
                SnapshotError::Kind::RestoreHookFailed,
                "after_restore",
                error.what()
            );
        } catch (...) {
            hook_error = make_error(
                SnapshotError::Kind::RestoreHookFailed,
                "after_restore",
                "Restore hook threw an unknown exception"
            );
        }
        if (hook_error) {
            auto message = hook_error->message;
            auto runtime_rollback = rollback_commit();
            if (!runtime_rollback) {
                message += "; runtime rollback failed: " +
                           runtime_rollback.error().message;
            }
            for (auto rollback = registry.rollback_hooks().rbegin();
                 rollback != registry.rollback_hooks().rend();
                 ++rollback) {
                try {
                    auto status = (*rollback)(world);
                    if (!status) {
                        message +=
                            "; rollback hook failed: " + status.error().message;
                    }
                } catch (const std::exception& error) {
                    message += "; rollback hook threw: ";
                    message += error.what();
                } catch (...) {
                    message += "; rollback hook threw an unknown exception";
                }
            }
            return failure(make_error(
                SnapshotError::Kind::RestoreHookFailed,
                "after_restore",
                std::move(message)
            ));
        }
    }
    return restored;
}

} // namespace ets::snapshot
