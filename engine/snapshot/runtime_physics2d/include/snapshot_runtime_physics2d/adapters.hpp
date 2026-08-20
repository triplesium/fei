#pragma once

#include "base/result.hpp"
#include "snapshot/world_snapshot.hpp"

namespace fei {

class World;

namespace snapshot_runtime_physics2d {

Status<snapshot::SnapshotError> configure_physics2d_adapters(
    World& world,
    snapshot::SnapshotRegistry& registry
);

} // namespace snapshot_runtime_physics2d
} // namespace fei
