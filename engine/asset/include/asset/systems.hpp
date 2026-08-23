#pragma once

#include "ecs/system_set.hpp"

namespace ets {

struct AssetSystems {
    struct ProcessLoadRequests : SystemSet<ProcessLoadRequests> {};
    struct ApplyAsyncLoads : SystemSet<ApplyAsyncLoads> {};
    struct CollectUnused : SystemSet<CollectUnused> {};
    struct TrackAssets : SystemSet<TrackAssets> {};
};

} // namespace ets
