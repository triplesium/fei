#pragma once

#include "ecs/system_set.hpp"

namespace fei {

struct WindowSystems {
    struct Prepare : SystemSet<Prepare> {};
    struct SyncSwapchain : SystemSet<SyncSwapchain> {};
};

struct Window {
    int width {0};
    int height {0};
};

} // namespace fei
