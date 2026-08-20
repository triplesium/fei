#include "runtime_host/quick_save.hpp"

#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "snapshot/world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct QuickSaveTestState {
    int value {};
};

void register_test_types() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    Registry::instance()
        .register_cls<QuickSaveTestState>(
            {"runtime_host_test"},
            "QuickSaveTestState"
        )
        .add_property("value", &QuickSaveTestState::value);
}

} // namespace

TEST_CASE(
    "Runtime Host maps PageUp and PageDown to quick-save requests",
    "[runtime-host][quick-save][input]"
) {
    World world;
    world.add_resource(KeyInput {});
    world.add_resource(runtime_host::QuickSaveHotkeyLatch {});
    world.add_resource(runtime_host::QuickSaveRequests {});

    world.resource<KeyInput>().press(KeyCode::PageUp);
    world.run_system_once(runtime_host::request_quick_save_hotkeys);
    CHECK(world.resource<runtime_host::QuickSaveRequests>().save);
    CHECK_FALSE(world.resource<runtime_host::QuickSaveRequests>().restore);

    world.resource<KeyInput>().clear();
    world.resource<KeyInput>().press(KeyCode::PageDown);
    world.run_system_once(runtime_host::request_quick_save_hotkeys);
    CHECK_FALSE(world.resource<runtime_host::QuickSaveRequests>().save);
    CHECK(world.resource<runtime_host::QuickSaveRequests>().restore);
}

TEST_CASE(
    "Runtime Host only restores once while PageDown remains held",
    "[runtime-host][quick-save][input]"
) {
    World world;
    world.add_resource(KeyInput {});
    world.add_resource(runtime_host::QuickSaveHotkeyLatch {});
    world.add_resource(runtime_host::QuickSaveRequests {});

    auto press_page_down = [&world]() {
        world.resource<KeyInput>().press(KeyCode::PageDown);
        world.run_system_once(runtime_host::request_quick_save_hotkeys);
    };

    press_page_down();
    REQUIRE(world.resource<runtime_host::QuickSaveRequests>().restore);

    world.resource<runtime_host::QuickSaveRequests>() = {};
    world.resource<KeyInput>() = {};
    press_page_down();
    CHECK_FALSE(world.resource<runtime_host::QuickSaveRequests>().restore);

    world.resource<KeyInput>().clear();
    world.run_system_once(runtime_host::request_quick_save_hotkeys);

    world.resource<KeyInput>().clear();
    press_page_down();
    CHECK(world.resource<runtime_host::QuickSaveRequests>().restore);
}

TEST_CASE(
    "Runtime Host quick-save restores strict snapshot state",
    "[runtime-host][quick-save][restore]"
) {
    register_test_types();

    World world;
    world.add_resource(runtime_host::QuickSaveRequests {});
    world.add_resource(snapshot::CheckpointStore {});
    world.add_resource(QuickSaveTestState {.value = 7});

    auto& registry = world.resource<snapshot::CheckpointStore>().registry();
    registry.resource<runtime_host::QuickSaveRequests>(
        snapshot::ResourcePolicy::Ignore
    );
    registry.resource<snapshot::CheckpointStore>(
        snapshot::ResourcePolicy::Ignore
    );
    registry.resource<QuickSaveTestState>(snapshot::ResourcePolicy::Snapshot);

    world.resource<runtime_host::QuickSaveRequests>().save = true;
    const auto saved = runtime_host::process_quick_save_requests(world);
    REQUIRE(saved.kind == runtime_host::QuickSaveOutcomeKind::Saved);

    world.resource<QuickSaveTestState>().value = 42;
    world.resource<runtime_host::QuickSaveRequests>().restore = true;
    const auto restored = runtime_host::process_quick_save_requests(world);
    REQUIRE(restored.kind == runtime_host::QuickSaveOutcomeKind::Restored);
    CHECK(world.resource<QuickSaveTestState>().value == 7);
    CHECK_FALSE(world.resource<runtime_host::QuickSaveRequests>().save);
    CHECK_FALSE(world.resource<runtime_host::QuickSaveRequests>().restore);
}
