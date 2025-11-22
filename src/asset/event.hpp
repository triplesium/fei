#pragma once

#include "asset/id.hpp"
namespace fei {

enum class AssetEventType {
    Added,
    Modified,
    Removed,
};

template<typename T>
struct AssetEvent {
    AssetEventType type;
    AssetId id;
};

} // namespace fei
