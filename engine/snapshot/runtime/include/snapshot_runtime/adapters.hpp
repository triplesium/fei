#pragma once

#include "app/plugin.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "refl/reflect.hpp"
#include "snapshot/world_snapshot.hpp"

namespace ets {

class World;

namespace snapshot_runtime {

ETS_REFLECT(Resource)
struct AutoCheckpointConfig {
    bool enabled {false};
    uint64 interval_frames {60};
    std::size_t retain {8};
    std::size_t max_bytes {64 * 1024 * 1024};
    bool strict {true};
};

struct AutoCheckpointState {
    uint64 frame {};
};

// Registers codecs for deterministic clock/RNG state, classifies physical
// input resources as rebuild-on-restore, and rebuilds derived global
// transforms. Only resources present in `world` are classified, so strict
// audit still exposes unknown game resources.
Status<snapshot::SnapshotError>
configure_builtin_adapters(World& world, snapshot::SnapshotRegistry& registry);

ETS_REFLECT(Plugin)
class SnapshotRuntimePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace snapshot_runtime
} // namespace ets
