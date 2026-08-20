#pragma once

#include "base/types.hpp"
#include "refl/reflect.hpp"

namespace fei {

// Serialized with the ECS world while the Luau VM itself remains live. A
// checkpoint may only restore when this generation still matches the loaded
// script registry.
FEI_REFLECT(Resource)
struct LuauSnapshotState {
    uint64 generation {};
};

} // namespace fei
