#include "snapshot_runtime/adapters.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "core/random.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "core/transform_plugin.hpp"
#include "ecs/world.hpp"
#include "input/input.hpp"
#include "refl/val.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace ets::snapshot_runtime {
namespace {

using serialization::DeserializeError;
using serialization::SerializedField;
using serialization::SerializedNode;
using serialization::SerializeError;

DeserializeError
invalid_node(TypeId type, std::string_view path, std::string message) {
    return DeserializeError {
        .kind = DeserializeError::Kind::InvalidNode,
        .type = type,
        .path = std::string(path),
        .message = std::move(message),
    };
}

const SerializedNode*
find_node(const SerializedNode& node, const std::string& name) {
    const auto* object = node.try_object();
    if (object == nullptr) {
        return nullptr;
    }
    const auto* found = serialization::find_field(*object, name);
    return found == nullptr ? nullptr : &found->value;
}

Optional<double> number(const SerializedNode* node) {
    if (node == nullptr) {
        return nullopt;
    }
    if (const auto* value = node->try_floating()) {
        return *value;
    }
    if (const auto* value = node->try_signed_integer()) {
        return static_cast<double>(*value);
    }
    if (const auto* value = node->try_unsigned_integer()) {
        return static_cast<double>(*value);
    }
    return nullopt;
}

serialization::ValueCodec time_codec() {
    return serialization::ValueCodec {
        .encode = [](Ref value, std::string_view)
            -> Result<SerializedNode, SerializeError> {
            const auto state = value.get_const<Time>().snapshot_state();
            return SerializedNode::object({
                SerializedField {
                    "delta",
                    SerializedNode::floating(state.delta),
                },
                SerializedField {
                    "elapsed",
                    SerializedNode::floating(state.elapsed),
                },
                SerializedField {
                    "max_delta",
                    SerializedNode::floating(state.max_delta),
                },
                SerializedField {
                    "fixed_delta",
                    state.fixed_delta ?
                        SerializedNode::floating(*state.fixed_delta) :
                        SerializedNode::null(),
                },
                SerializedField {
                    "time_scale",
                    SerializedNode::floating(state.time_scale),
                },
            });
        },
        .decode = [](const SerializedNode& node,
                     std::string_view path) -> Result<Val, DeserializeError> {
            const auto delta = number(find_node(node, "delta"));
            const auto elapsed = number(find_node(node, "elapsed"));
            const auto max_delta = number(find_node(node, "max_delta"));
            const auto time_scale = number(find_node(node, "time_scale"));
            const auto* fixed_node = find_node(node, "fixed_delta");
            Optional<float> fixed_delta;
            if (fixed_node != nullptr && !fixed_node->is_null()) {
                const auto fixed = number(fixed_node);
                if (!fixed || !std::isfinite(*fixed) || *fixed <= 0.0) {
                    return failure(invalid_node(
                        type_id<Time>(),
                        path,
                        "Time fixed_delta must be null or positive"
                    ));
                }
                fixed_delta = static_cast<float>(*fixed);
            }
            if (!delta || !elapsed || !max_delta || !time_scale ||
                !std::isfinite(*delta) || !std::isfinite(*elapsed) ||
                !std::isfinite(*max_delta) || !std::isfinite(*time_scale) ||
                *elapsed < 0.0 || *max_delta <= 0.0) {
                return failure(invalid_node(
                    type_id<Time>(),
                    path,
                    "Time snapshot contains invalid numeric state"
                ));
            }
            Time restored;
            restored.restore_snapshot_state(
                TimeSnapshotState {
                    .delta = static_cast<float>(*delta),
                    .elapsed = static_cast<float>(*elapsed),
                    .max_delta = static_cast<float>(*max_delta),
                    .fixed_delta = fixed_delta,
                    .time_scale = static_cast<float>(*time_scale),
                }
            );
            return make_val<Time>(std::move(restored));
        },
    };
}

serialization::ValueCodec fixed_time_codec() {
    return serialization::ValueCodec {
        .encode = [](Ref value, std::string_view)
            -> Result<SerializedNode, SerializeError> {
            const auto state = value.get_const<FixedTime>().snapshot_state();
            return SerializedNode::object({
                SerializedField {
                    "timestep",
                    SerializedNode::floating(state.timestep),
                },
                SerializedField {
                    "overstep",
                    SerializedNode::floating(state.overstep),
                },
                SerializedField {
                    "elapsed",
                    SerializedNode::floating(state.elapsed),
                },
            });
        },
        .decode = [](const SerializedNode& node,
                     std::string_view path) -> Result<Val, DeserializeError> {
            const auto timestep = number(find_node(node, "timestep"));
            const auto overstep = number(find_node(node, "overstep"));
            const auto elapsed = number(find_node(node, "elapsed"));
            if (!timestep || !overstep || !elapsed ||
                !std::isfinite(*timestep) || !std::isfinite(*overstep) ||
                !std::isfinite(*elapsed) || *timestep <= 0.0 ||
                *overstep < 0.0 || *elapsed < 0.0) {
                return failure(invalid_node(
                    type_id<FixedTime>(),
                    path,
                    "FixedTime snapshot contains invalid numeric state"
                ));
            }
            FixedTime restored;
            restored.restore_snapshot_state(
                FixedTimeSnapshotState {
                    .timestep = *timestep,
                    .overstep = *overstep,
                    .elapsed = *elapsed,
                }
            );
            return make_val<FixedTime>(std::move(restored));
        },
    };
}

template<class T>
void rebuild_if_present(World& world, snapshot::SnapshotRegistry& registry) {
    if (world.has_resource<T>()) {
        registry.resource<T>(snapshot::ResourcePolicy::Rebuild);
    }
}

void update_auto_checkpoint(WorldRef world) {
    auto& config = world->resource<AutoCheckpointConfig>();
    auto& state = world->resource<AutoCheckpointState>();
    ++state.frame;
    if (!config.enabled || config.retain == 0 || config.interval_frames == 0 ||
        state.frame % config.interval_frames != 0) {
        return;
    }
    auto& store = world->resource<snapshot::CheckpointStore>();
    auto limits = store.limits();
    limits.max_count = config.retain;
    limits.max_bytes = config.max_bytes;
    limits.eviction = snapshot::CheckpointEviction::Oldest;
    store.set_limits(limits);
    auto created = store.create(
        "auto-" + std::to_string(state.frame),
        *world,
        config.strict
    );
    if (!created) {
        warn(
            "Automatic checkpoint failed at frame {}: {}",
            state.frame,
            created.error().message
        );
    }
}

Status<snapshot::SnapshotError> rebuild_global_transforms(World& world) {
    world.run_system_once(sync_global_transforms_2d);
    world.run_system_once(propagate_transforms_2d);
    world.run_system_once(sync_global_transforms);
    world.run_system_once(propagate_transforms);
    return {};
}

} // namespace

Status<snapshot::SnapshotError>
configure_builtin_adapters(World& world, snapshot::SnapshotRegistry& registry) {
    if (world.has_resource<Time>()) {
        registry.resource<Time>(snapshot::ResourcePolicy::Snapshot);
        if (registry.codecs().find(type_id<Time>()) == nullptr) {
            registry.codecs().register_codec<Time>(time_codec());
        }
    }
    if (world.has_resource<FixedTime>()) {
        registry.resource<FixedTime>(snapshot::ResourcePolicy::Snapshot);
        if (registry.codecs().find(type_id<FixedTime>()) == nullptr) {
            registry.codecs().register_codec<FixedTime>(fixed_time_codec());
        }
    }
    if (world.has_resource<DeterministicRng>()) {
        registry.resource<DeterministicRng>(snapshot::ResourcePolicy::Snapshot);
    }

    registry.component<GlobalTransform2d>(snapshot::ComponentPolicy::Rebuild);
    registry.component<GlobalTransform3d>(snapshot::ComponentPolicy::Rebuild);
    registry.on_after_restore(rebuild_global_transforms);
    registry.on_restore_rollback(rebuild_global_transforms);

    rebuild_if_present<KeyInput>(world, registry);
    rebuild_if_present<MouseInput>(world, registry);
    rebuild_if_present<MouseScrollInput>(world, registry);
    rebuild_if_present<CharacterInput>(world, registry);
    rebuild_if_present<VirtualInput>(world, registry);
    rebuild_if_present<Events<KeyEvent>>(world, registry);
    rebuild_if_present<Events<MouseButtonEvent>>(world, registry);
    rebuild_if_present<Events<MouseMoveEvent>>(world, registry);
    rebuild_if_present<Events<MouseScrollEvent>>(world, registry);
    rebuild_if_present<Events<CharacterEvent>>(world, registry);
    rebuild_if_present<Events<InputFocusLost>>(world, registry);
    registry.on_after_restore(
        [](World& restored) -> Status<snapshot::SnapshotError> {
            if (restored.has_resource<KeyInput>()) {
                restored.resource<KeyInput>().clear();
            }
            if (restored.has_resource<MouseInput>()) {
                restored.resource<MouseInput>().clear();
            }
            if (restored.has_resource<MouseScrollInput>()) {
                restored.resource<MouseScrollInput>().clear();
            }
            if (restored.has_resource<CharacterInput>()) {
                restored.resource<CharacterInput>().clear();
            }
            if (restored.has_resource<VirtualInput>()) {
                restored.resource<VirtualInput>().clear();
            }
            return {};
        }
    );
    return {};
}

void SnapshotRuntimePlugin::setup(App& app) {
    if (!app.has_resource<snapshot::CheckpointStore>()) {
        app.add_resource(snapshot::CheckpointStore {});
    }
    if (!app.has_resource<AutoCheckpointConfig>()) {
        app.add_resource(AutoCheckpointConfig {});
    }
    if (!app.has_resource<AutoCheckpointState>()) {
        app.add_resource(AutoCheckpointState {});
    }
    auto& registry = app.resource<snapshot::CheckpointStore>().registry();
    registry.resource<AutoCheckpointConfig>(snapshot::ResourcePolicy::Ignore);
    registry.resource<AutoCheckpointState>(snapshot::ResourcePolicy::Ignore);
    app.add_systems(PreUpdate, update_auto_checkpoint);
}

} // namespace ets::snapshot_runtime
