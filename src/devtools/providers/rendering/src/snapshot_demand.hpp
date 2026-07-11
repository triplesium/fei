#pragma once

#include <string_view>

namespace fei::devtools::rendering {

inline constexpr const char* c_render_graph_capability =
    "rendering.render_graph";
inline constexpr const char* c_graphics_cache_capability = "graphics.cache";

struct SnapshotDemand {
    bool render_graph {false};
    bool graphics_cache {false};

    void include(std::string_view capability) {
        render_graph |= capability == c_render_graph_capability;
        graphics_cache |= capability == c_graphics_cache_capability;
    }

    bool any() const { return render_graph || graphics_cache; }
};

} // namespace fei::devtools::rendering
