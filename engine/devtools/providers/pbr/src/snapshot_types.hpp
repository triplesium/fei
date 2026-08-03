#pragma once

#include "base/optional.hpp"
#include "devtools/types.hpp"
#include "pbr/passes/target.hpp"
#include "refl/reflect.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei::devtools::pbr {

FEI_REFLECT()
struct RenderTargetViewSnapshot {
    std::string id;
    std::string label;
    bool available {false};
    Optional<BlobRef> preview;
    std::string visualization;
};

FEI_REFLECT()
struct RenderTargetSnapshot {
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

FEI_REFLECT()
struct RenderTargetsSnapshot {
    std::vector<BlobRef> previews;
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
