#pragma once

#include "ecs/system_set.hpp"

namespace fei {

struct AssetSystems {
    struct ApplyAsyncLoads : SystemSet<ApplyAsyncLoads> {};
    struct TrackAssets : SystemSet<TrackAssets> {};
};

} // namespace fei
