#pragma once

#include <string_view>

namespace fei::devtools::rendering {

inline constexpr const char* c_render_schedule_capability =
    "rendering.render_schedule";
inline constexpr const char* c_graphics_cache_capability = "graphics.cache";

struct SnapshotDemand {
    bool render_schedule {false};
    bool graphics_cache {false};

    void include(std::string_view capability) {
        render_schedule |= capability == c_render_schedule_capability;
        graphics_cache |= capability == c_graphics_cache_capability;
    }

    bool any() const { return render_schedule || graphics_cache; }
};

} // namespace fei::devtools::rendering
