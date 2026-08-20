#pragma once

#include "ecs/system_params.hpp"
#include "window/input.hpp"

#include <string>

namespace fei {

class World;

namespace runtime_host {

inline constexpr const char* c_quick_save_checkpoint_name = "quick-save";

struct QuickSaveRequests {
    bool save {false};
    bool restore {false};
};

struct QuickSaveHotkeyLatch {
    bool save_held {false};
    bool restore_held {false};
};

enum class QuickSaveOutcomeKind {
    None,
    Saved,
    Restored,
    Failed,
};

struct QuickSaveOutcome {
    QuickSaveOutcomeKind kind {QuickSaveOutcomeKind::None};
    std::string message;
};

void request_quick_save_hotkeys(
    ResRO<KeyInput> input,
    ResRW<QuickSaveHotkeyLatch> hotkeys,
    ResRW<QuickSaveRequests> requests
);

QuickSaveOutcome process_quick_save_requests(World& world);

} // namespace runtime_host
} // namespace fei
