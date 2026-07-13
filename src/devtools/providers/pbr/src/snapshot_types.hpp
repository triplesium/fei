#pragma once

#include "pbr/passes/target.hpp"
#include "refl/reflect.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei::devtools::pbr {

struct FEI_REFLECT RenderTargetViewSnapshot {
    std::string id;
    std::string label;
    bool available {false};
    std::string blob_capability;
    std::string visualization;
};

struct FEI_REFLECT RenderTargetSnapshot {
    std::string id;
    std::string label;
    bool available {false};
    std::string format;
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::uint32_t depth {0};
    std::uint32_t mip_levels {0};
    std::uint32_t layers {0};
    std::vector<RenderTargetViewSnapshot> views;
};

struct FEI_REFLECT RenderTargetsSnapshot {
    bool available {false};
    std::uint64_t total_targets {0};
    std::uint64_t available_targets {0};
    std::uint64_t total_views {0};
    std::uint64_t available_views {0};
    std::vector<RenderTargetSnapshot> targets;
};

RenderTargetsSnapshot
make_render_targets_snapshot(const DeferredViewTargets& targets);

} // namespace fei::devtools::pbr
