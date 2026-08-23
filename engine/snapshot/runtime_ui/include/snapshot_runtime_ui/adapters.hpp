#pragma once

#include "base/result.hpp"
#include "snapshot/world_snapshot.hpp"

namespace ets {

class World;

namespace snapshot_runtime_ui {

// Configures exact snapshotting for authored UI state and deterministic
// rebuilding for layout, text, clipping, render order, and transient input.
// Asset adapters must be configured first so Image and Font handles have
// semantic codecs.
Status<snapshot::SnapshotError>
configure_ui_adapters(World& world, snapshot::SnapshotRegistry& registry);

} // namespace snapshot_runtime_ui
} // namespace ets
