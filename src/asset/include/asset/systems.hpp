#pragma once

#include "ecs/system_set.hpp"

namespace fei {

struct AssetSystems {
    struct ProcessLoadRequests : SystemSet<ProcessLoadRequests> {};
    struct ApplyAsyncLoads : SystemSet<ApplyAsyncLoads> {};
    struct CollectUnused : SystemSet<CollectUnused> {};
    struct TrackAssets : SystemSet<TrackAssets> {};
};

} // namespace fei
