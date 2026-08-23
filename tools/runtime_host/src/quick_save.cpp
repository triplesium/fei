#include "runtime_host/quick_save.hpp"

#include "ecs/world.hpp"
#include "snapshot/world_snapshot.hpp"

#include <string>

namespace ets::runtime_host {

void request_quick_save_hotkeys(
    ResRO<KeyInput> input,
    ResRW<QuickSaveHotkeyLatch> hotkeys,
    ResRW<QuickSaveRequests> requests
) {
    const auto restore_held = input->pressed(KeyCode::PageDown);
    const auto save_held = input->pressed(KeyCode::PageUp);

    if (restore_held && !hotkeys->restore_held) {
        requests->restore = true;
        requests->save = false;
    } else if (save_held && !hotkeys->save_held) {
        requests->save = true;
    }

    hotkeys->restore_held = restore_held;
    hotkeys->save_held = save_held;
}

QuickSaveOutcome process_quick_save_requests(World& world) {
    auto& requests = world.resource<QuickSaveRequests>();
    const auto save = requests.save;
    const auto restore = requests.restore;
    requests = QuickSaveRequests {};

    if (!save && !restore) {
        return {};
    }

    auto& checkpoints = world.resource<snapshot::CheckpointStore>();
    if (restore) {
        auto restored =
            checkpoints.restore(c_quick_save_checkpoint_name, world);
        if (!restored) {
            return {
                .kind = QuickSaveOutcomeKind::Failed,
                .message =
                    "Quick-save restore failed: " + restored.error().message,
            };
        }
        return {
            .kind = QuickSaveOutcomeKind::Restored,
            .message = "Restored quick-save",
        };
    }

    auto created =
        checkpoints.create(c_quick_save_checkpoint_name, world, true);
    if (!created) {
        return {
            .kind = QuickSaveOutcomeKind::Failed,
            .message = "Quick-save failed: " + created.error().message,
        };
    }
    return {
        .kind = QuickSaveOutcomeKind::Saved,
        .message = "Saved quick-save (" + std::to_string(created->byte_size) +
                   " bytes)",
    };
}

} // namespace ets::runtime_host
