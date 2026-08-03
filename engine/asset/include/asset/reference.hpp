#pragma once

#include "asset/path.hpp"
#include "asset/uuid.hpp"
#include "base/optional.hpp"

namespace fei {

struct AssetReference {
    Optional<AssetUuid> id;
    AssetPath fallback_path;
};

} // namespace fei
