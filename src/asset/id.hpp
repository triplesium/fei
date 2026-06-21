#pragma once
#include <cstdint>
#include <limits>

namespace fei {
using AssetId = std::uint32_t;
inline constexpr AssetId invalid_asset_id = std::numeric_limits<AssetId>::max();
} // namespace fei
