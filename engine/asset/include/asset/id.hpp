#pragma once
#include "refl/type.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace ets {
using AssetId = std::uint32_t;
inline constexpr AssetId invalid_asset_id = std::numeric_limits<AssetId>::max();

struct AssetKey {
    TypeId type;
    AssetId id {invalid_asset_id};

    bool operator==(const AssetKey& other) const = default;
};
} // namespace ets

namespace std {
template<>
struct hash<ets::AssetKey> { // NOLINT(readability-identifier-naming)
    size_t operator()(const ets::AssetKey& key) const {
        auto seed = std::hash<ets::TypeId> {}(key.type);
        seed ^= std::hash<ets::AssetId> {}(key.id) + 0x9e3779b9 + (seed << 6) +
                (seed >> 2);
        return seed;
    }
};
} // namespace std
