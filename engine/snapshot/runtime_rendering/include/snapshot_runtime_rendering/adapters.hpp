#pragma once

#include "base/result.hpp"
#include "snapshot/world_snapshot.hpp"

namespace fei {

class World;

namespace snapshot_runtime_rendering {

// Keeps host-owned graphics state alive and rebuilds the transient link
// between Main World entities and their Render World counterparts.
Status<snapshot::SnapshotError> configure_rendering_adapters(
    World& world,
    snapshot::SnapshotRegistry& registry
);

} // namespace snapshot_runtime_rendering
} // namespace fei
