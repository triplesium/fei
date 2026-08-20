#include "snapshot/archive.hpp"

#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "serialization/json_archive.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#    define NOMINMAX
#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#endif

namespace fei::snapshot {
namespace {

using serialization::SerializedField;
using serialization::SerializedNode;

constexpr std::string_view c_archive_magic {"fei.world-snapshot"};

SnapshotError
archive_error(SnapshotError::Kind kind, std::string path, std::string message) {
    return SnapshotError {
        .kind = kind,
        .path = std::move(path),
        .message = std::move(message),
    };
}

class SignatureBuilder {
  private:
    std::uint64_t m_hash {14695981039346656037ULL};

  public:
    void append(std::string_view value) {
        append_number(value.size());
        for (const auto character : value) {
            m_hash ^= static_cast<std::uint8_t>(character);
            m_hash *= 1099511628211ULL;
        }
    }

    template<typename T>
    void append_number(T value) {
        char buffer[32];
        const auto [end, error] =
            std::to_chars(std::begin(buffer), std::end(buffer), value);
        if (error == std::errc {}) {
            for (auto cursor = std::begin(buffer); cursor != end; ++cursor) {
                m_hash ^= static_cast<std::uint8_t>(*cursor);
                m_hash *= 1099511628211ULL;
            }
        }
        m_hash ^= 0xffU;
        m_hash *= 1099511628211ULL;
    }

    std::string finish() const {
        char buffer[17];
        const auto [end, error] =
            std::to_chars(std::begin(buffer), std::end(buffer), m_hash, 16);
        return error == std::errc {} ? std::string(buffer, end) :
                                       std::string("invalid");
    }
};

void append_system_signature(
    SignatureBuilder& signature,
    const SystemRuntimeState& state
) {
    signature.append_number(state.params.size());
    for (const auto& param : state.params) {
        // Parameter values and initialization state are mutable. Only a
        // custom parameter's stable type token contributes to compatibility.
        signature.append_number(param.parameter_type);
    }
}

Result<const SerializedNode::Object*, SnapshotError> expect_object(
    const SerializedNode& node,
    std::string_view path,
    std::initializer_list<std::string_view> fields
) {
    const auto* object = node.try_object();
    if (object == nullptr) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            std::string(path),
            "Expected an object"
        ));
    }
    if (object->size() != fields.size()) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            std::string(path),
            "Object has an unexpected field count"
        ));
    }
    for (const auto required : fields) {
        const auto count = std::ranges::count_if(
            *object,
            [required](const SerializedField& field) {
                return field.name == required;
            }
        );
        if (count != 1) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveFormatFailed,
                std::string(path),
                "Object must contain exactly one '" + std::string(required) +
                    "' field"
            ));
        }
    }
    return object;
}

const SerializedNode&
field(const SerializedNode::Object& object, std::string_view name) {
    return serialization::find_field(object, std::string(name))->value;
}

Result<const SerializedNode::Array*, SnapshotError>
expect_array(const SerializedNode& node, std::string path) {
    const auto* array = node.try_array();
    if (array == nullptr) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            std::move(path),
            "Expected an array"
        ));
    }
    return array;
}

Result<std::uint64_t, SnapshotError>
expect_unsigned(const SerializedNode& node, std::string path) {
    if (const auto* value = node.try_unsigned_integer()) {
        return *value;
    }
    if (const auto* value = node.try_signed_integer(); value && *value >= 0) {
        return static_cast<std::uint64_t>(*value);
    }
    return failure(archive_error(
        SnapshotError::Kind::ArchiveFormatFailed,
        std::move(path),
        "Expected a non-negative integer"
    ));
}

template<typename T>
Result<T, SnapshotError>
expect_bounded_unsigned(const SerializedNode& node, std::string path) {
    auto value = expect_unsigned(node, path);
    if (!value) {
        return failure(std::move(value.error()));
    }
    if (*value > std::numeric_limits<T>::max()) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            std::move(path),
            "Unsigned integer is out of range"
        ));
    }
    return static_cast<T>(*value);
}

Result<std::string, SnapshotError>
expect_string(const SerializedNode& node, std::string path) {
    const auto* value = node.try_string();
    if (value == nullptr) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            std::move(path),
            "Expected a string"
        ));
    }
    return *value;
}

Result<bool, SnapshotError>
expect_bool(const SerializedNode& node, std::string path) {
    const auto* value = node.try_bool();
    if (value == nullptr) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            std::move(path),
            "Expected a boolean"
        ));
    }
    return *value;
}

SerializedNode ticks_node(ComponentTicks ticks) {
    return SerializedNode::object({
        SerializedField {
            "added",
            SerializedNode::unsigned_integer(ticks.added),
        },
        SerializedField {
            "changed",
            SerializedNode::unsigned_integer(ticks.changed),
        },
    });
}

Result<ComponentTicks, SnapshotError>
decode_ticks(const SerializedNode& node, const std::string& path) {
    auto object = expect_object(node, path, {"added", "changed"});
    if (!object) {
        return failure(std::move(object.error()));
    }
    auto added = expect_unsigned(field(**object, "added"), path + ".added");
    if (!added) {
        return failure(std::move(added.error()));
    }
    auto changed =
        expect_unsigned(field(**object, "changed"), path + ".changed");
    if (!changed) {
        return failure(std::move(changed.error()));
    }
    return ComponentTicks {.added = *added, .changed = *changed};
}

SerializedNode param_state_node(const SystemParamRuntimeState& state) {
    return SerializedNode::object({
        SerializedField {
            "kind",
            SerializedNode::unsigned_integer(
                static_cast<std::uint8_t>(state.kind)
            ),
        },
        SerializedField {
            "value",
            SerializedNode::unsigned_integer(state.value),
        },
        SerializedField {
            "parameter_type",
            SerializedNode::unsigned_integer(state.parameter_type),
        },
    });
}

Result<SystemParamRuntimeState, SnapshotError>
decode_param_state(const SerializedNode& node, const std::string& path) {
    auto object =
        expect_object(node, path, {"kind", "value", "parameter_type"});
    if (!object) {
        return failure(std::move(object.error()));
    }
    auto kind = expect_bounded_unsigned<std::uint8_t>(
        field(**object, "kind"),
        path + ".kind"
    );
    if (!kind) {
        return failure(std::move(kind.error()));
    }
    if (*kind >
        static_cast<std::uint8_t>(SystemParamRuntimeStateKind::Deferred)) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            path + ".kind",
            "Unknown system parameter runtime state kind"
        ));
    }
    auto value = expect_unsigned(field(**object, "value"), path + ".value");
    if (!value) {
        return failure(std::move(value.error()));
    }
    auto parameter_type = expect_unsigned(
        field(**object, "parameter_type"),
        path + ".parameter_type"
    );
    if (!parameter_type) {
        return failure(std::move(parameter_type.error()));
    }
    return SystemParamRuntimeState {
        .kind = static_cast<SystemParamRuntimeStateKind>(*kind),
        .value = *value,
        .parameter_type = *parameter_type,
    };
}

Result<SerializedNode, SnapshotError>
system_state_node(const SystemRuntimeState& state, const std::string& path) {
    if (state.executor.kind != SystemExecutorRuntimeStateKind::Stateless) {
        return failure(archive_error(
            SnapshotError::Kind::PersistentStateUnsupported,
            path + ".executor",
            "Stateful system callable has no persistent snapshot codec"
        ));
    }
    SerializedNode::Array params;
    params.reserve(state.params.size());
    for (const auto& param : state.params) {
        params.push_back(param_state_node(param));
    }
    return SerializedNode::object({
        SerializedField {
            "last_run",
            SerializedNode::unsigned_integer(state.last_run),
        },
        SerializedField {
            "executor",
            SerializedNode::string("stateless"),
        },
        SerializedField {
            "params",
            SerializedNode::array(std::move(params)),
        },
    });
}

Result<SystemRuntimeState, SnapshotError>
decode_system_state(const SerializedNode& node, const std::string& path) {
    auto object = expect_object(node, path, {"last_run", "executor", "params"});
    if (!object) {
        return failure(std::move(object.error()));
    }
    auto last_run =
        expect_unsigned(field(**object, "last_run"), path + ".last_run");
    if (!last_run) {
        return failure(std::move(last_run.error()));
    }
    auto executor =
        expect_string(field(**object, "executor"), path + ".executor");
    if (!executor) {
        return failure(std::move(executor.error()));
    }
    if (*executor != "stateless") {
        return failure(archive_error(
            SnapshotError::Kind::PersistentStateUnsupported,
            path + ".executor",
            "Archive contains an unsupported stateful system callable"
        ));
    }
    auto params_array =
        expect_array(field(**object, "params"), path + ".params");
    if (!params_array) {
        return failure(std::move(params_array.error()));
    }
    std::vector<SystemParamRuntimeState> params;
    params.reserve((*params_array)->size());
    for (std::size_t index = 0; index < (*params_array)->size(); ++index) {
        auto param = decode_param_state(
            (**params_array)[index],
            path + ".params[" + std::to_string(index) + "]"
        );
        if (!param) {
            return failure(std::move(param.error()));
        }
        params.push_back(std::move(*param));
    }
    return SystemRuntimeState {
        .last_run = *last_run,
        .executor = SystemExecutorRuntimeState::stateless(),
        .params = std::move(params),
    };
}

Result<SerializedNode, SnapshotError> scheduled_system_node(
    const ScheduledSystemRuntimeState& state,
    const std::string& path
) {
    auto system = system_state_node(state.system, path + ".system");
    if (!system) {
        return failure(std::move(system.error()));
    }
    SerializedNode::Array conditions;
    conditions.reserve(state.conditions.size());
    for (std::size_t index = 0; index < state.conditions.size(); ++index) {
        auto condition = system_state_node(
            state.conditions[index],
            path + ".conditions[" + std::to_string(index) + "]"
        );
        if (!condition) {
            return failure(std::move(condition.error()));
        }
        conditions.push_back(std::move(*condition));
    }
    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::unsigned_integer(state.id),
        },
        SerializedField {"system", std::move(*system)},
        SerializedField {
            "conditions",
            SerializedNode::array(std::move(conditions)),
        },
    });
}

Result<ScheduledSystemRuntimeState, SnapshotError>
decode_scheduled_system(const SerializedNode& node, const std::string& path) {
    auto object = expect_object(node, path, {"id", "system", "conditions"});
    if (!object) {
        return failure(std::move(object.error()));
    }
    auto id =
        expect_bounded_unsigned<SystemId>(field(**object, "id"), path + ".id");
    if (!id) {
        return failure(std::move(id.error()));
    }
    auto system =
        decode_system_state(field(**object, "system"), path + ".system");
    if (!system) {
        return failure(std::move(system.error()));
    }
    auto conditions_array =
        expect_array(field(**object, "conditions"), path + ".conditions");
    if (!conditions_array) {
        return failure(std::move(conditions_array.error()));
    }
    std::vector<SystemRuntimeState> conditions;
    conditions.reserve((*conditions_array)->size());
    for (std::size_t index = 0; index < (*conditions_array)->size(); ++index) {
        auto condition = decode_system_state(
            (**conditions_array)[index],
            path + ".conditions[" + std::to_string(index) + "]"
        );
        if (!condition) {
            return failure(std::move(condition.error()));
        }
        conditions.push_back(std::move(*condition));
    }
    return ScheduledSystemRuntimeState {
        .id = *id,
        .system = std::move(*system),
        .conditions = std::move(conditions),
    };
}

Result<SerializedNode, SnapshotError>
runtime_state_node(const WorldRuntimeState& runtime) {
    SerializedNode::Array schedules;
    schedules.reserve(runtime.schedules.schedules.size());
    for (std::size_t schedule_index = 0;
         schedule_index < runtime.schedules.schedules.size();
         ++schedule_index) {
        const auto& schedule = runtime.schedules.schedules[schedule_index];
        SerializedNode::Array systems;
        systems.reserve(schedule.systems.size());
        for (std::size_t system_index = 0;
             system_index < schedule.systems.size();
             ++system_index) {
            auto system = scheduled_system_node(
                schedule.systems[system_index],
                "snapshot.runtime.schedules[" + std::to_string(schedule_index) +
                    "].systems[" + std::to_string(system_index) + "]"
            );
            if (!system) {
                return failure(std::move(system.error()));
            }
            systems.push_back(std::move(*system));
        }
        schedules.push_back(
            SerializedNode::object({
                SerializedField {
                    "id",
                    SerializedNode::unsigned_integer(schedule.id),
                },
                SerializedField {
                    "systems",
                    SerializedNode::array(std::move(systems)),
                },
            })
        );
    }

    SerializedNode::Array registered;
    registered.reserve(runtime.registered_systems.size());
    for (std::size_t index = 0; index < runtime.registered_systems.size();
         ++index) {
        const auto& state = runtime.registered_systems[index];
        auto system = system_state_node(
            state.system,
            "snapshot.runtime.registered_systems[" + std::to_string(index) +
                "].system"
        );
        if (!system) {
            return failure(std::move(system.error()));
        }
        registered.push_back(
            SerializedNode::object({
                SerializedField {
                    "id",
                    SerializedNode::unsigned_integer(state.id),
                },
                SerializedField {"system", std::move(*system)},
            })
        );
    }

    struct RemovedEntry {
        std::string type_name;
        RemovedComponentBuffer::SnapshotState state;
    };
    std::vector<RemovedEntry> removed_entries;
    removed_entries.reserve(runtime.removed_components.buffers().size());
    for (const auto& [type, buffer] : runtime.removed_components.buffers()) {
        auto reflected = Registry::instance().try_get_type(type);
        if (!reflected) {
            return failure(archive_error(
                SnapshotError::Kind::TypeNotFound,
                "snapshot.runtime.removed_components",
                reflected.error().message
            ));
        }
        removed_entries.push_back(
            RemovedEntry {
                .type_name = reflected->name(),
                .state = buffer.snapshot_state(),
            }
        );
    }
    std::ranges::sort(removed_entries, {}, &RemovedEntry::type_name);
    SerializedNode::Array removed;
    removed.reserve(removed_entries.size());
    auto entity_array = [](const std::vector<Entity>& entities) {
        SerializedNode::Array result;
        result.reserve(entities.size());
        for (const auto entity : entities) {
            result.push_back(SerializedNode::unsigned_integer(entity.value));
        }
        return result;
    };
    for (auto& entry : removed_entries) {
        removed.push_back(
            SerializedNode::object({
                SerializedField {
                    "type",
                    SerializedNode::string(std::move(entry.type_name)),
                },
                SerializedField {
                    "previous_start",
                    SerializedNode::unsigned_integer(
                        entry.state.previous_start
                    ),
                },
                SerializedField {
                    "previous",
                    SerializedNode::array(entity_array(entry.state.previous)),
                },
                SerializedField {
                    "current_start",
                    SerializedNode::unsigned_integer(entry.state.current_start),
                },
                SerializedField {
                    "current",
                    SerializedNode::array(entity_array(entry.state.current)),
                },
                SerializedField {
                    "event_count",
                    SerializedNode::unsigned_integer(entry.state.event_count),
                },
            })
        );
    }

    return SerializedNode::object({
        SerializedField {
            "change_tick",
            SerializedNode::unsigned_integer(runtime.change_tick),
        },
        SerializedField {
            "topology_generation",
            SerializedNode::unsigned_integer(
                runtime.schedules.topology_generation
            ),
        },
        SerializedField {
            "schedules",
            SerializedNode::array(std::move(schedules)),
        },
        SerializedField {
            "registered_system_generation",
            SerializedNode::unsigned_integer(
                runtime.registered_system_generation
            ),
        },
        SerializedField {
            "registered_systems",
            SerializedNode::array(std::move(registered)),
        },
        SerializedField {
            "removed_components",
            SerializedNode::array(std::move(removed)),
        },
    });
}

Result<std::vector<Entity>, SnapshotError>
decode_entity_array(const SerializedNode& node, const std::string& path) {
    auto array = expect_array(node, path);
    if (!array) {
        return failure(std::move(array.error()));
    }
    std::vector<Entity> entities;
    entities.reserve((*array)->size());
    for (std::size_t index = 0; index < (*array)->size(); ++index) {
        auto value = expect_bounded_unsigned<std::uint32_t>(
            (**array)[index],
            path + "[" + std::to_string(index) + "]"
        );
        if (!value) {
            return failure(std::move(value.error()));
        }
        entities.push_back(Entity {*value});
    }
    return entities;
}

Result<WorldRuntimeState, SnapshotError>
decode_runtime_state(const SerializedNode& node, const std::string& path) {
    auto object = expect_object(
        node,
        path,
        {
            "change_tick",
            "topology_generation",
            "schedules",
            "registered_system_generation",
            "registered_systems",
            "removed_components",
        }
    );
    if (!object) {
        return failure(std::move(object.error()));
    }
    auto change_tick =
        expect_unsigned(field(**object, "change_tick"), path + ".change_tick");
    if (!change_tick) {
        return failure(std::move(change_tick.error()));
    }
    auto topology = expect_unsigned(
        field(**object, "topology_generation"),
        path + ".topology_generation"
    );
    if (!topology) {
        return failure(std::move(topology.error()));
    }

    auto schedules_array =
        expect_array(field(**object, "schedules"), path + ".schedules");
    if (!schedules_array) {
        return failure(std::move(schedules_array.error()));
    }
    std::vector<ScheduleRuntimeState> schedules;
    schedules.reserve((*schedules_array)->size());
    for (std::size_t schedule_index = 0;
         schedule_index < (*schedules_array)->size();
         ++schedule_index) {
        const auto schedule_path =
            path + ".schedules[" + std::to_string(schedule_index) + "]";
        auto schedule_object = expect_object(
            (**schedules_array)[schedule_index],
            schedule_path,
            {"id", "systems"}
        );
        if (!schedule_object) {
            return failure(std::move(schedule_object.error()));
        }
        auto id = expect_bounded_unsigned<ScheduleId>(
            field(**schedule_object, "id"),
            schedule_path + ".id"
        );
        if (!id) {
            return failure(std::move(id.error()));
        }
        auto systems_array = expect_array(
            field(**schedule_object, "systems"),
            schedule_path + ".systems"
        );
        if (!systems_array) {
            return failure(std::move(systems_array.error()));
        }
        std::vector<ScheduledSystemRuntimeState> systems;
        systems.reserve((*systems_array)->size());
        for (std::size_t system_index = 0;
             system_index < (*systems_array)->size();
             ++system_index) {
            auto system = decode_scheduled_system(
                (**systems_array)[system_index],
                schedule_path + ".systems[" + std::to_string(system_index) + "]"
            );
            if (!system) {
                return failure(std::move(system.error()));
            }
            systems.push_back(std::move(*system));
        }
        schedules.push_back(
            ScheduleRuntimeState {.id = *id, .systems = std::move(systems)}
        );
    }

    auto registered_generation = expect_unsigned(
        field(**object, "registered_system_generation"),
        path + ".registered_system_generation"
    );
    if (!registered_generation) {
        return failure(std::move(registered_generation.error()));
    }
    auto registered_array = expect_array(
        field(**object, "registered_systems"),
        path + ".registered_systems"
    );
    if (!registered_array) {
        return failure(std::move(registered_array.error()));
    }
    std::vector<RegisteredSystemRuntimeState> registered;
    registered.reserve((*registered_array)->size());
    for (std::size_t index = 0; index < (*registered_array)->size(); ++index) {
        const auto registered_path =
            path + ".registered_systems[" + std::to_string(index) + "]";
        auto registered_object = expect_object(
            (**registered_array)[index],
            registered_path,
            {"id", "system"}
        );
        if (!registered_object) {
            return failure(std::move(registered_object.error()));
        }
        auto id = expect_bounded_unsigned<SystemId>(
            field(**registered_object, "id"),
            registered_path + ".id"
        );
        if (!id) {
            return failure(std::move(id.error()));
        }
        auto system = decode_system_state(
            field(**registered_object, "system"),
            registered_path + ".system"
        );
        if (!system) {
            return failure(std::move(system.error()));
        }
        registered.push_back(
            RegisteredSystemRuntimeState {
                .id = *id,
                .system = std::move(*system),
            }
        );
    }

    auto removed_array = expect_array(
        field(**object, "removed_components"),
        path + ".removed_components"
    );
    if (!removed_array) {
        return failure(std::move(removed_array.error()));
    }
    RemovedComponentEvents removed;
    std::unordered_set<TypeId> removed_types;
    for (std::size_t index = 0; index < (*removed_array)->size(); ++index) {
        const auto removed_path =
            path + ".removed_components[" + std::to_string(index) + "]";
        auto removed_object = expect_object(
            (**removed_array)[index],
            removed_path,
            {
                "type",
                "previous_start",
                "previous",
                "current_start",
                "current",
                "event_count",
            }
        );
        if (!removed_object) {
            return failure(std::move(removed_object.error()));
        }
        auto type_name = expect_string(
            field(**removed_object, "type"),
            removed_path + ".type"
        );
        if (!type_name) {
            return failure(std::move(type_name.error()));
        }
        auto reflected = Registry::instance().try_get_type_exact(*type_name);
        if (!reflected) {
            return failure(archive_error(
                SnapshotError::Kind::TypeNotFound,
                removed_path + ".type",
                reflected.error().message
            ));
        }
        if (!removed_types.insert(reflected->id()).second) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveFormatFailed,
                removed_path + ".type",
                "Duplicate removed-component type"
            ));
        }
        auto previous_start = expect_bounded_unsigned<std::size_t>(
            field(**removed_object, "previous_start"),
            removed_path + ".previous_start"
        );
        if (!previous_start) {
            return failure(std::move(previous_start.error()));
        }
        auto previous = decode_entity_array(
            field(**removed_object, "previous"),
            removed_path + ".previous"
        );
        if (!previous) {
            return failure(std::move(previous.error()));
        }
        auto current_start = expect_bounded_unsigned<std::size_t>(
            field(**removed_object, "current_start"),
            removed_path + ".current_start"
        );
        if (!current_start) {
            return failure(std::move(current_start.error()));
        }
        auto current = decode_entity_array(
            field(**removed_object, "current"),
            removed_path + ".current"
        );
        if (!current) {
            return failure(std::move(current.error()));
        }
        auto event_count = expect_bounded_unsigned<std::size_t>(
            field(**removed_object, "event_count"),
            removed_path + ".event_count"
        );
        if (!event_count) {
            return failure(std::move(event_count.error()));
        }
        if (*previous_start > *current_start ||
            previous->size() != *current_start - *previous_start ||
            *current_start > *event_count ||
            current->size() != *event_count - *current_start) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveFormatFailed,
                removed_path,
                "Removed-component event counters are inconsistent"
            ));
        }
        RemovedComponentBuffer buffer;
        buffer.restore_snapshot_state(
            RemovedComponentBuffer::SnapshotState {
                .previous = std::move(*previous),
                .previous_start = *previous_start,
                .current = std::move(*current),
                .current_start = *current_start,
                .event_count = *event_count,
            }
        );
        removed.set_buffer(reflected->id(), std::move(buffer));
    }

    return WorldRuntimeState {
        .change_tick = *change_tick,
        .schedules =
            SchedulesRuntimeState {
                .topology_generation = *topology,
                .schedules = std::move(schedules),
            },
        .registered_system_generation = *registered_generation,
        .registered_systems = std::move(registered),
        .removed_components = std::move(removed),
    };
}

Result<SerializedNode, SnapshotError>
snapshot_node(const WorldSnapshot& snapshot) {
    SerializedNode::Array entities;
    entities.reserve(snapshot.entities.size());
    for (const auto& entity : snapshot.entities) {
        SerializedNode::Array components;
        components.reserve(entity.components.size());
        for (const auto& component : entity.components) {
            components.push_back(
                SerializedNode::object({
                    SerializedField {
                        "type",
                        SerializedNode::string(component.type_name),
                    },
                    SerializedField {"value", component.value},
                    SerializedField {"ticks", ticks_node(component.ticks)},
                    SerializedField {
                        "derived",
                        SerializedNode::boolean(component.derived),
                    },
                })
            );
        }
        entities.push_back(
            SerializedNode::object({
                SerializedField {
                    "id",
                    SerializedNode::unsigned_integer(entity.id),
                },
                SerializedField {
                    "runtime_id",
                    SerializedNode::unsigned_integer(entity.runtime_id.value),
                },
                SerializedField {
                    "components",
                    SerializedNode::array(std::move(components)),
                },
            })
        );
    }

    SerializedNode::Array resources;
    resources.reserve(snapshot.resources.size());
    for (const auto& resource : snapshot.resources) {
        resources.push_back(
            SerializedNode::object({
                SerializedField {
                    "type",
                    SerializedNode::string(resource.type_name),
                },
                SerializedField {"value", resource.value},
                SerializedField {"ticks", ticks_node(resource.ticks)},
            })
        );
    }

    SerializedNode::Array dynamic_events;
    dynamic_events.reserve(snapshot.dynamic_events.size());
    for (const auto& channel : snapshot.dynamic_events) {
        dynamic_events.push_back(
            SerializedNode::object({
                SerializedField {
                    "type",
                    SerializedNode::string(channel.type_name),
                },
                SerializedField {
                    "previous",
                    SerializedNode::array(channel.previous),
                },
                SerializedField {
                    "previous_start",
                    SerializedNode::unsigned_integer(channel.previous_start),
                },
                SerializedField {
                    "current",
                    SerializedNode::array(channel.current),
                },
                SerializedField {
                    "current_start",
                    SerializedNode::unsigned_integer(channel.current_start),
                },
                SerializedField {
                    "event_count",
                    SerializedNode::unsigned_integer(channel.event_count),
                },
            })
        );
    }

    auto runtime = runtime_state_node(snapshot.runtime);
    if (!runtime) {
        return failure(std::move(runtime.error()));
    }
    return SerializedNode::object({
        SerializedField {
            "entities",
            SerializedNode::array(std::move(entities)),
        },
        SerializedField {
            "resources",
            SerializedNode::array(std::move(resources)),
        },
        SerializedField {
            "dynamic_events_present",
            SerializedNode::boolean(snapshot.dynamic_events_present),
        },
        SerializedField {
            "dynamic_events",
            SerializedNode::array(std::move(dynamic_events)),
        },
        SerializedField {"runtime", std::move(*runtime)},
    });
}

Result<WorldSnapshot, SnapshotError> decode_snapshot(
    const SerializedNode& node,
    std::uint32_t version,
    const std::string& path
) {
    auto object = expect_object(
        node,
        path,
        {
            "entities",
            "resources",
            "dynamic_events_present",
            "dynamic_events",
            "runtime",
        }
    );
    if (!object) {
        return failure(std::move(object.error()));
    }

    auto entities_array =
        expect_array(field(**object, "entities"), path + ".entities");
    if (!entities_array) {
        return failure(std::move(entities_array.error()));
    }
    std::vector<SnapshotEntity> entities;
    entities.reserve((*entities_array)->size());
    for (std::size_t entity_index = 0; entity_index < (*entities_array)->size();
         ++entity_index) {
        const auto entity_path =
            path + ".entities[" + std::to_string(entity_index) + "]";
        auto entity_object = expect_object(
            (**entities_array)[entity_index],
            entity_path,
            {"id", "runtime_id", "components"}
        );
        if (!entity_object) {
            return failure(std::move(entity_object.error()));
        }
        auto id =
            expect_unsigned(field(**entity_object, "id"), entity_path + ".id");
        if (!id) {
            return failure(std::move(id.error()));
        }
        auto runtime_id = expect_bounded_unsigned<std::uint32_t>(
            field(**entity_object, "runtime_id"),
            entity_path + ".runtime_id"
        );
        if (!runtime_id) {
            return failure(std::move(runtime_id.error()));
        }
        auto components_array = expect_array(
            field(**entity_object, "components"),
            entity_path + ".components"
        );
        if (!components_array) {
            return failure(std::move(components_array.error()));
        }
        std::vector<SnapshotComponent> components;
        components.reserve((*components_array)->size());
        for (std::size_t component_index = 0;
             component_index < (*components_array)->size();
             ++component_index) {
            const auto component_path = entity_path + ".components[" +
                                        std::to_string(component_index) + "]";
            auto component_object = expect_object(
                (**components_array)[component_index],
                component_path,
                {"type", "value", "ticks", "derived"}
            );
            if (!component_object) {
                return failure(std::move(component_object.error()));
            }
            auto type = expect_string(
                field(**component_object, "type"),
                component_path + ".type"
            );
            if (!type) {
                return failure(std::move(type.error()));
            }
            auto ticks = decode_ticks(
                field(**component_object, "ticks"),
                component_path + ".ticks"
            );
            if (!ticks) {
                return failure(std::move(ticks.error()));
            }
            auto derived = expect_bool(
                field(**component_object, "derived"),
                component_path + ".derived"
            );
            if (!derived) {
                return failure(std::move(derived.error()));
            }
            components.push_back(
                SnapshotComponent {
                    .type_name = std::move(*type),
                    .value = field(**component_object, "value"),
                    .ticks = *ticks,
                    .derived = *derived,
                }
            );
        }
        entities.push_back(
            SnapshotEntity {
                .id = *id,
                .runtime_id = Entity {*runtime_id},
                .components = std::move(components),
            }
        );
    }

    auto resources_array =
        expect_array(field(**object, "resources"), path + ".resources");
    if (!resources_array) {
        return failure(std::move(resources_array.error()));
    }
    std::vector<SnapshotResource> resources;
    resources.reserve((*resources_array)->size());
    for (std::size_t index = 0; index < (*resources_array)->size(); ++index) {
        const auto resource_path =
            path + ".resources[" + std::to_string(index) + "]";
        auto resource_object = expect_object(
            (**resources_array)[index],
            resource_path,
            {"type", "value", "ticks"}
        );
        if (!resource_object) {
            return failure(std::move(resource_object.error()));
        }
        auto type = expect_string(
            field(**resource_object, "type"),
            resource_path + ".type"
        );
        if (!type) {
            return failure(std::move(type.error()));
        }
        auto ticks = decode_ticks(
            field(**resource_object, "ticks"),
            resource_path + ".ticks"
        );
        if (!ticks) {
            return failure(std::move(ticks.error()));
        }
        resources.push_back(
            SnapshotResource {
                .type_name = std::move(*type),
                .value = field(**resource_object, "value"),
                .ticks = *ticks,
            }
        );
    }

    auto dynamic_present = expect_bool(
        field(**object, "dynamic_events_present"),
        path + ".dynamic_events_present"
    );
    if (!dynamic_present) {
        return failure(std::move(dynamic_present.error()));
    }
    auto dynamic_array = expect_array(
        field(**object, "dynamic_events"),
        path + ".dynamic_events"
    );
    if (!dynamic_array) {
        return failure(std::move(dynamic_array.error()));
    }
    if (!*dynamic_present && !(*dynamic_array)->empty()) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            path + ".dynamic_events",
            "Dynamic event channels require dynamic_events_present"
        ));
    }
    std::vector<SnapshotDynamicEventChannel> dynamic_events;
    dynamic_events.reserve((*dynamic_array)->size());
    for (std::size_t index = 0; index < (*dynamic_array)->size(); ++index) {
        const auto channel_path =
            path + ".dynamic_events[" + std::to_string(index) + "]";
        auto channel_object = expect_object(
            (**dynamic_array)[index],
            channel_path,
            {
                "type",
                "previous",
                "previous_start",
                "current",
                "current_start",
                "event_count",
            }
        );
        if (!channel_object) {
            return failure(std::move(channel_object.error()));
        }
        auto type = expect_string(
            field(**channel_object, "type"),
            channel_path + ".type"
        );
        if (!type) {
            return failure(std::move(type.error()));
        }
        auto previous_array = expect_array(
            field(**channel_object, "previous"),
            channel_path + ".previous"
        );
        if (!previous_array) {
            return failure(std::move(previous_array.error()));
        }
        auto previous_start = expect_bounded_unsigned<std::size_t>(
            field(**channel_object, "previous_start"),
            channel_path + ".previous_start"
        );
        if (!previous_start) {
            return failure(std::move(previous_start.error()));
        }
        auto current_array = expect_array(
            field(**channel_object, "current"),
            channel_path + ".current"
        );
        if (!current_array) {
            return failure(std::move(current_array.error()));
        }
        auto current_start = expect_bounded_unsigned<std::size_t>(
            field(**channel_object, "current_start"),
            channel_path + ".current_start"
        );
        if (!current_start) {
            return failure(std::move(current_start.error()));
        }
        auto event_count = expect_bounded_unsigned<std::size_t>(
            field(**channel_object, "event_count"),
            channel_path + ".event_count"
        );
        if (!event_count) {
            return failure(std::move(event_count.error()));
        }
        if (*previous_start > *current_start ||
            (*previous_array)->size() != *current_start - *previous_start ||
            *current_start > *event_count ||
            (*current_array)->size() != *event_count - *current_start) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveFormatFailed,
                channel_path,
                "Dynamic event counters are inconsistent"
            ));
        }
        dynamic_events.push_back(
            SnapshotDynamicEventChannel {
                .type_name = std::move(*type),
                .previous = **previous_array,
                .previous_start = *previous_start,
                .current = **current_array,
                .current_start = *current_start,
                .event_count = *event_count,
            }
        );
    }

    auto runtime =
        decode_runtime_state(field(**object, "runtime"), path + ".runtime");
    if (!runtime) {
        return failure(std::move(runtime.error()));
    }
    return WorldSnapshot {
        .version = version,
        .entities = std::move(entities),
        .resources = std::move(resources),
        .dynamic_events_present = *dynamic_present,
        .dynamic_events = std::move(dynamic_events),
        .runtime = std::move(*runtime),
    };
}

SerializedNode metadata_node(const SnapshotArchiveMetadata& metadata) {
    return SerializedNode::object({
        SerializedField {
            "project",
            SerializedNode::string(metadata.project),
        },
        SerializedField {
            "engine_build",
            SerializedNode::string(metadata.engine_build),
        },
        SerializedField {
            "runtime_signature",
            SerializedNode::string(metadata.runtime_signature),
        },
        SerializedField {
            "script_hash",
            SerializedNode::string(metadata.script_hash),
        },
    });
}

Result<SnapshotArchiveMetadata, SnapshotError>
decode_metadata(const SerializedNode& node, const std::string& path) {
    auto object = expect_object(
        node,
        path,
        {"project", "engine_build", "runtime_signature", "script_hash"}
    );
    if (!object) {
        return failure(std::move(object.error()));
    }
    auto project = expect_string(field(**object, "project"), path + ".project");
    if (!project) {
        return failure(std::move(project.error()));
    }
    auto engine_build =
        expect_string(field(**object, "engine_build"), path + ".engine_build");
    if (!engine_build) {
        return failure(std::move(engine_build.error()));
    }
    auto runtime_signature = expect_string(
        field(**object, "runtime_signature"),
        path + ".runtime_signature"
    );
    if (!runtime_signature) {
        return failure(std::move(runtime_signature.error()));
    }
    auto script_hash =
        expect_string(field(**object, "script_hash"), path + ".script_hash");
    if (!script_hash) {
        return failure(std::move(script_hash.error()));
    }
    return SnapshotArchiveMetadata {
        .project = std::move(*project),
        .engine_build = std::move(*engine_build),
        .runtime_signature = std::move(*runtime_signature),
        .script_hash = std::move(*script_hash),
    };
}

Status<SnapshotError> publish_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
#if defined(_WIN32)
    if (!MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveIoFailed,
            destination.string(),
            "Failed to publish snapshot archive (Windows error " +
                std::to_string(GetLastError()) + ")"
        ));
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveIoFailed,
            destination.string(),
            "Failed to publish snapshot archive: " + error.message()
        ));
    }
#endif
    return {};
}

} // namespace

Result<std::string, SnapshotError>
runtime_compatibility_signature(const World& world) {
    auto runtime = world.capture_runtime_state();
    if (!runtime) {
        return failure(archive_error(
            SnapshotError::Kind::SerializeFailed,
            runtime.error().path.empty() ? "runtime" :
                                           "runtime." + runtime.error().path,
            std::move(runtime.error().message)
        ));
    }

    SignatureBuilder signature;
    signature.append("fei.runtime-compatibility.v1");
    signature.append_number(runtime->schedules.topology_generation);
    signature.append_number(runtime->schedules.schedules.size());
    for (const auto& schedule : runtime->schedules.schedules) {
        signature.append_number(schedule.id);
        signature.append_number(schedule.systems.size());
        for (const auto& scheduled : schedule.systems) {
            signature.append_number(scheduled.id);
            append_system_signature(signature, scheduled.system);
            signature.append_number(scheduled.conditions.size());
            for (const auto& condition : scheduled.conditions) {
                append_system_signature(signature, condition);
            }
        }
    }
    signature.append_number(runtime->registered_system_generation);
    signature.append_number(runtime->registered_systems.size());
    for (const auto& registered : runtime->registered_systems) {
        signature.append_number(registered.id);
        append_system_signature(signature, registered.system);
    }

    auto& registry = Registry::instance();
    std::vector<const Type*> reflected_types;
    reflected_types.reserve(registry.types().size());
    for (const auto& [_, reflected] : registry.types()) {
        reflected_types.push_back(&reflected);
    }
    std::ranges::sort(
        reflected_types,
        {},
        [](const Type* reflected) -> const std::string& {
            return reflected->name();
        }
    );
    signature.append_number(reflected_types.size());
    for (const auto* reflected : reflected_types) {
        signature.append(reflected->name());
        signature.append_number(reflected->size());
        signature.append_number(reflected->align());

        auto cls = registry.try_get_cls(reflected->id());
        if (!cls) {
            signature.append_number(0U);
            continue;
        }
        auto properties = cls->get_properties();
        std::ranges::sort(properties, {}, [](const Property* property) {
            return property->name();
        });
        signature.append_number(properties.size());
        for (const auto* property : properties) {
            signature.append(property->name());
            auto property_type = registry.try_get_type(property->type_id());
            if (property_type) {
                signature.append(property_type->name());
            } else {
                signature.append_number(property->type_id().id());
            }
        }
    }
    return signature.finish();
}

Result<SerializedNode, SnapshotError>
encode_archive(const SnapshotArchive& archive) {
    if (archive.version != SnapshotArchive::c_current_version) {
        return failure(archive_error(
            SnapshotError::Kind::UnsupportedVersion,
            "archive_version",
            "Unsupported snapshot archive version"
        ));
    }
    if (archive.snapshot.version != WorldSnapshot::c_current_version) {
        return failure(archive_error(
            SnapshotError::Kind::UnsupportedVersion,
            "snapshot_version",
            "Unsupported world snapshot version"
        ));
    }
    auto snapshot = snapshot_node(archive.snapshot);
    if (!snapshot) {
        return failure(std::move(snapshot.error()));
    }
    return SerializedNode::object({
        SerializedField {
            "magic",
            SerializedNode::string(std::string(c_archive_magic)),
        },
        SerializedField {
            "archive_version",
            SerializedNode::unsigned_integer(archive.version),
        },
        SerializedField {
            "snapshot_version",
            SerializedNode::unsigned_integer(archive.snapshot.version),
        },
        SerializedField {"compatibility", metadata_node(archive.metadata)},
        SerializedField {"snapshot", std::move(*snapshot)},
    });
}

Result<SnapshotArchive, SnapshotError>
decode_archive(const SerializedNode& node) {
    auto object = expect_object(
        node,
        "archive",
        {
            "magic",
            "archive_version",
            "snapshot_version",
            "compatibility",
            "snapshot",
        }
    );
    if (!object) {
        return failure(std::move(object.error()));
    }
    auto magic = expect_string(field(**object, "magic"), "archive.magic");
    if (!magic) {
        return failure(std::move(magic.error()));
    }
    if (*magic != c_archive_magic) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            "archive.magic",
            "File is not a Fei world snapshot archive"
        ));
    }
    auto archive_version = expect_bounded_unsigned<std::uint32_t>(
        field(**object, "archive_version"),
        "archive.archive_version"
    );
    if (!archive_version) {
        return failure(std::move(archive_version.error()));
    }
    if (*archive_version != SnapshotArchive::c_current_version) {
        return failure(archive_error(
            SnapshotError::Kind::UnsupportedVersion,
            "archive.archive_version",
            "Unsupported snapshot archive version"
        ));
    }
    auto snapshot_version = expect_bounded_unsigned<std::uint32_t>(
        field(**object, "snapshot_version"),
        "archive.snapshot_version"
    );
    if (!snapshot_version) {
        return failure(std::move(snapshot_version.error()));
    }
    if (*snapshot_version != WorldSnapshot::c_current_version) {
        return failure(archive_error(
            SnapshotError::Kind::UnsupportedVersion,
            "archive.snapshot_version",
            "Unsupported world snapshot version"
        ));
    }
    auto metadata = decode_metadata(
        field(**object, "compatibility"),
        "archive.compatibility"
    );
    if (!metadata) {
        return failure(std::move(metadata.error()));
    }
    auto snapshot = decode_snapshot(
        field(**object, "snapshot"),
        *snapshot_version,
        "archive.snapshot"
    );
    if (!snapshot) {
        return failure(std::move(snapshot.error()));
    }
    return SnapshotArchive {
        .version = *archive_version,
        .metadata = std::move(*metadata),
        .snapshot = std::move(*snapshot),
    };
}

Status<SnapshotError> validate_archive_metadata(
    const SnapshotArchiveMetadata& actual,
    const SnapshotArchiveMetadata& expected
) {
    auto mismatch = [](std::string path, std::string name) {
        return failure(archive_error(
            SnapshotError::Kind::IncompatibleArchive,
            std::move(path),
            "Snapshot " + std::move(name) +
                " does not match the current runtime"
        ));
    };
    if (actual.project != expected.project) {
        return mismatch("compatibility.project", "project");
    }
    if (actual.engine_build != expected.engine_build) {
        return mismatch("compatibility.engine_build", "engine build");
    }
    if (actual.runtime_signature != expected.runtime_signature) {
        return mismatch("compatibility.runtime_signature", "runtime signature");
    }
    if (actual.script_hash != expected.script_hash) {
        return mismatch("compatibility.script_hash", "script hash");
    }
    return {};
}

Status<SnapshotError> save_archive_file(
    const std::filesystem::path& path,
    const SnapshotArchive& archive
) {
    if (path.empty()) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveIoFailed,
            "path",
            "Snapshot archive path cannot be empty"
        ));
    }
    auto encoded = encode_archive(archive);
    if (!encoded) {
        return failure(std::move(encoded.error()));
    }
    auto json = serialization::write_json(*encoded, 2);
    if (!json) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            path.string(),
            std::move(json.error().message)
        ));
    }

    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveIoFailed,
                parent.string(),
                "Failed to create snapshot directory: " + error.message()
            ));
        }
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveIoFailed,
                temporary.string(),
                "Failed to create temporary snapshot archive"
            ));
        }
        stream.write(json->data(), static_cast<std::streamsize>(json->size()));
        stream.put('\n');
        stream.flush();
        if (!stream) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveIoFailed,
                temporary.string(),
                "Failed to write temporary snapshot archive"
            ));
        }
    }

    auto published = publish_file(temporary, path);
    if (!published) {
        std::filesystem::remove(temporary, error);
        return published;
    }
    return {};
}

Result<SnapshotArchive, SnapshotError> load_archive_file(
    const std::filesystem::path& path,
    const SnapshotArchiveMetadata& expected,
    SnapshotArchiveLimits limits
) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveIoFailed,
            path.string(),
            "Failed to open snapshot archive"
        ));
    }
    const auto end = stream.tellg();
    if (end < 0) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveIoFailed,
            path.string(),
            "Failed to determine snapshot archive size"
        ));
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > limits.max_file_bytes ||
        size > static_cast<std::uint64_t>(
                   std::numeric_limits<std::size_t>::max()
               )) {
        return failure(archive_error(
            SnapshotError::Kind::CheckpointTooLarge,
            path.string(),
            "Snapshot archive exceeds the configured file size limit"
        ));
    }
    stream.seekg(0, std::ios::beg);
    std::string json(static_cast<std::size_t>(size), '\0');
    if (!json.empty()) {
        stream.read(json.data(), static_cast<std::streamsize>(json.size()));
        if (!stream) {
            return failure(archive_error(
                SnapshotError::Kind::ArchiveIoFailed,
                path.string(),
                "Failed to read snapshot archive"
            ));
        }
    }
    auto node = serialization::read_json(json);
    if (!node) {
        return failure(archive_error(
            SnapshotError::Kind::ArchiveFormatFailed,
            path.string(),
            std::move(node.error().message)
        ));
    }
    auto archive = decode_archive(*node);
    if (!archive) {
        return failure(std::move(archive.error()));
    }
    auto compatible = validate_archive_metadata(archive->metadata, expected);
    if (!compatible) {
        return failure(std::move(compatible.error()));
    }
    return archive;
}

Result<CheckpointInfo, SnapshotError> CheckpointStore::export_file(
    std::string_view name,
    const std::filesystem::path& path,
    const SnapshotArchiveMetadata& metadata
) const {
    const auto found = m_entries.find(name);
    if (found == m_entries.end()) {
        return failure(archive_error(
            SnapshotError::Kind::CheckpointNotFound,
            "name",
            "Checkpoint '" + std::string(name) + "' does not exist"
        ));
    }
    auto saved = save_archive_file(
        path,
        SnapshotArchive {
            .metadata = metadata,
            .snapshot = found->second.snapshot,
        }
    );
    if (!saved) {
        return failure(std::move(saved.error()));
    }
    return CheckpointInfo {
        .name = std::string(name),
        .revision = found->second.revision,
        .entity_count = found->second.snapshot.entities.size(),
        .resource_count =
            found->second.snapshot.resources.size() +
            (found->second.snapshot.dynamic_events_present ? 1 : 0),
        .byte_size = found->second.byte_size,
    };
}

Result<CheckpointInfo, SnapshotError> CheckpointStore::import_file(
    std::string name,
    const std::filesystem::path& path,
    const SnapshotArchiveMetadata& expected
) {
    auto archive = load_archive_file(path, expected);
    if (!archive) {
        return failure(std::move(archive.error()));
    }
    return insert(std::move(name), std::move(archive->snapshot));
}

} // namespace fei::snapshot
