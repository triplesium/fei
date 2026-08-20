#pragma once

#include "base/types.hpp"
#include "refl/reflect.hpp"

namespace fei {

FEI_REFLECT(Resource)
struct DeterministicRng {
    static constexpr uint64 c_default_seed = 0x9e3779b97f4a7c15ULL;

    uint64 state {c_default_seed};

    void seed(uint64 value) { state = value == 0 ? c_default_seed : value; }

    uint64 next_u64() {
        state ^= state >> 12U;
        state ^= state << 25U;
        state ^= state >> 27U;
        return state * 0x2545f4914f6cdd1dULL;
    }

    uint64 range(uint64 upper_exclusive) {
        return upper_exclusive == 0 ? 0 : next_u64() % upper_exclusive;
    }
};

} // namespace fei
