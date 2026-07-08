#pragma once

#include "devtools/types.hpp"
#include "ecs/fwd.hpp"

#include <string>

namespace fei {

class World;

namespace devtools {

Entity declare_capability(
    World& world,
    std::string id,
    std::string label,
    BlobCapability desc
);

Entity declare_capability(
    World& world,
    std::string id,
    std::string label,
    SnapshotCapability desc
);

Entity declare_capability(
    World& world,
    std::string id,
    std::string label,
    CommandCapability desc
);

} // namespace devtools

} // namespace fei
