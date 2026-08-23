#pragma once

#include "asset/id.hpp"
namespace ets {

enum class AssetEventType {
    Added,
    Modified,
    Removed,
    Failed,
};

template<typename T>
struct AssetEvent {
    AssetEventType type;
    AssetId id;
};

} // namespace ets
