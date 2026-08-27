#include "snapshot_runtime_luau/adapters.hpp"

#include "asset/assets.hpp"
#include "ecs/dynamic/events.hpp"
#include "ecs/world.hpp"
#include "refl/val.hpp"
#include "scripting_luau/asset.hpp"
#include "scripting_luau/execution_pool.hpp"
#include "scripting_luau/runtime.hpp"
#include "scripting_luau/script_system_registry.hpp"
#include "scripting_luau/snapshot_state.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <string>

namespace ets::snapshot_runtime_luau {
namespace {

serialization::ValueCodec snapshot_state_codec() {
    return serialization::ValueCodec {
        .encode =
            [](Ref value, std::string_view) -> Result<
                                                serialization::SerializedNode,
                                                serialization::SerializeError> {
            return serialization::SerializedNode::unsigned_integer(
                value.get_const<LuauSnapshotState>().generation
            );
        },
        .decode = [](const serialization::SerializedNode& node,
                     std::string_view path)
            -> Result<Val, serialization::DeserializeError> {
            const auto* generation = node.try_unsigned_integer();
            if (generation == nullptr) {
                return failure(
                    serialization::DeserializeError {
                        .kind =
                            serialization::DeserializeError::Kind::InvalidNode,
                        .type = type_id<LuauSnapshotState>(),
                        .path = std::string(path),
                        .message = "Luau snapshot generation must be unsigned",
                    }
                );
            }
            return make_val<LuauSnapshotState>(
                LuauSnapshotState {.generation = *generation}
            );
        },
    };
}

} // namespace

Status<snapshot::SnapshotError>
configure_luau_adapters(World& world, snapshot::SnapshotRegistry& registry) {
    if (!world.has_resource<LuauRuntime>() ||
        !world.has_resource<LuauExecutionPool>() ||
        !world.has_resource<LuauScriptSystemRegistry>() ||
        !world.has_resource<LuauSnapshotState>()) {
        return failure(
            snapshot::SnapshotError {
                .kind = snapshot::SnapshotError::Kind::InvalidConfiguration,
                .path = "luau",
                .message = "Luau snapshot adapter requires LuauScriptingPlugin",
            }
        );
    }

    registry.resource<LuauRuntime>(snapshot::ResourcePolicy::Ignore);
    registry.resource<LuauExecutionPool>(snapshot::ResourcePolicy::Ignore);
    registry.resource<LuauScriptSystemRegistry>(
        snapshot::ResourcePolicy::Ignore
    );
    registry.resource<LuauSnapshotState>(snapshot::ResourcePolicy::Snapshot);
    registry.resource<DynamicEvents>(snapshot::ResourcePolicy::Snapshot);
    if (registry.codecs().find(type_id<LuauSnapshotState>()) == nullptr) {
        registry.codecs().register_codec<LuauSnapshotState>(
            snapshot_state_codec()
        );
    }
    if (world.has_resource<Assets<LuauScriptAsset>>()) {
        registry.resource<Assets<LuauScriptAsset>>(
            snapshot::ResourcePolicy::Ignore
        );
    }

    auto& scripts = world.resource<LuauScriptSystemRegistry>();
    world.resource<LuauSnapshotState>().generation =
        scripts.snapshot_generation();
    for (const auto type : scripts.snapshot_resource_types()) {
        registry.set_resource_policy(type, snapshot::ResourcePolicy::Snapshot);
    }

    registry.on_after_restore(
        [](World& restored) -> Status<snapshot::SnapshotError> {
            const auto checkpoint_generation =
                restored.resource<LuauSnapshotState>().generation;
            const auto loaded_generation =
                restored.resource<LuauScriptSystemRegistry>()
                    .snapshot_generation();
            if (checkpoint_generation != loaded_generation) {
                return failure(
                    snapshot::SnapshotError {
                        .kind =
                            snapshot::SnapshotError::Kind::RestoreHookFailed,
                        .path = "luau.generation",
                        .message =
                            "Checkpoint was captured with Luau generation " +
                            std::to_string(checkpoint_generation) +
                            ", but generation " +
                            std::to_string(loaded_generation) +
                            " is currently loaded",
                    }
                );
            }
            return {};
        }
    );
    return {};
}

} // namespace ets::snapshot_runtime_luau
