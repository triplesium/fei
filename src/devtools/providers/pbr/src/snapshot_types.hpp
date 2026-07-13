#pragma once

#include "pbr/passes/target.hpp"
#include "refl/reflect.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei::devtools::pbr {

struct FEI_REFLECT RenderTargetSnapshot {
    std::string id;
    std::string label;
    bool available {false};
    bool directly_capturable {false};
    std::string blob_capability;
    std::string format;
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::uint32_t depth {0};
    std::uint32_t mip_levels {0};
    std::uint32_t layers {0};
};

struct FEI_REFLECT RenderTargetsSnapshot {
    bool available {false};
    std::uint64_t total_targets {0};
    std::uint64_t available_targets {0};
    std::uint64_t directly_capturable_targets {0};
    std::vector<RenderTargetSnapshot> targets;
};

RenderTargetsSnapshot
make_render_targets_snapshot(const DeferredViewTargets& targets);

} // namespace fei::devtools::pbr
