#pragma once

#include "base/result.hpp"
#include "snapshot/world_snapshot.hpp"

namespace fei {

class World;

namespace snapshot_runtime_asset {

// Call after asset plugins have registered their types. Asset services and
// caches remain live across restore; ECS handles reconnect by UUID/path.
Status<snapshot::SnapshotError>
configure_asset_adapters(World& world, snapshot::SnapshotRegistry& registry);

} // namespace snapshot_runtime_asset
} // namespace fei
