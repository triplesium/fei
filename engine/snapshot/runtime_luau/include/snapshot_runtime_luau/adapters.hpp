#pragma once

#include "base/result.hpp"
#include "snapshot/world_snapshot.hpp"

namespace ets {

class World;

namespace snapshot_runtime_luau {

// Keeps VM/runtime infrastructure live, snapshots script-declared ECS state,
// and rejects restores after the loaded Luau module generation changes.
Status<snapshot::SnapshotError>
configure_luau_adapters(World& world, snapshot::SnapshotRegistry& registry);

} // namespace snapshot_runtime_luau
} // namespace ets
