#include "snapshot_runtime/adapters.hpp"

#include "app/app.hpp"
#include "core/random.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/world.hpp"
#include "input/input.hpp"
#include "refl/generated.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "Runtime snapshot adapters restore clocks RNG and rebuild input",
    "[snapshot][runtime][determinism]"
) {
    register_generated_reflection();

    World world;
    Time time;
    time.set_fixed_delta(0.125F);
    time.tick();
    time.tick();
    world.add_resource(std::move(time));
    FixedTime fixed;
    fixed.set_timestep(0.25F);
    fixed.accumulate_overstep(0.375F);
    REQUIRE(fixed.expend());
    world.add_resource(std::move(fixed));
    world.add_resource(DeterministicRng {.state = 12345});
    world.add_resource(KeyInput {});
    world.add_resource(MouseInput {});
    world.add_resource(MouseScrollInput {});
    world.add_resource(CharacterInput {});
    world.add_resource(VirtualInput {});

    snapshot::SnapshotRegistry registry;
    REQUIRE(snapshot_runtime::configure_builtin_adapters(world, registry));
    const auto coverage = snapshot::audit(world, registry);
    CHECK(coverage.ready);
    CHECK(coverage.complete);

    const auto expected_time = world.resource<Time>().snapshot_state();
    const auto expected_fixed = world.resource<FixedTime>().snapshot_state();
    const auto expected_rng = world.resource<DeterministicRng>().state;
    auto captured = snapshot::capture(world, registry);
    REQUIRE(captured);

    world.resource<Time>().reset_elapsed_time(99.0F);
    world.resource<FixedTime>().reset();
    world.resource<DeterministicRng>().next_u64();
    world.resource<KeyInput>().press(KeyCode::A);
    world.resource<MouseInput>().press(MouseButton::Left);
    world.resource<MouseScrollInput>().scroll({2.0F, 3.0F});
    world.resource<CharacterInput>().push(U'x');
    const KeyCode virtual_keys[] {KeyCode::B};
    world.resource<VirtualInput>().set_pressed_keys(virtual_keys);

    REQUIRE(snapshot::restore(world, *captured, registry));
    const auto restored_time = world.resource<Time>().snapshot_state();
    const auto restored_fixed = world.resource<FixedTime>().snapshot_state();
    CHECK(restored_time.delta == expected_time.delta);
    CHECK(restored_time.elapsed == expected_time.elapsed);
    CHECK(restored_time.fixed_delta == expected_time.fixed_delta);
    CHECK(restored_fixed.timestep == expected_fixed.timestep);
    CHECK(restored_fixed.overstep == expected_fixed.overstep);
    CHECK(restored_fixed.elapsed == expected_fixed.elapsed);
    CHECK(world.resource<DeterministicRng>().state == expected_rng);
    CHECK_FALSE(world.resource<KeyInput>().pressed(KeyCode::A));
    CHECK_FALSE(world.resource<MouseInput>().pressed(MouseButton::Left));
    CHECK(world.resource<MouseScrollInput>().delta() == Vector2::Zero);
    CHECK(world.resource<CharacterInput>().characters().empty());
    CHECK_FALSE(world.resource<VirtualInput>().pressed(KeyCode::B));
}

TEST_CASE(
    "Runtime snapshot adapters rebuild derived global transforms",
    "[snapshot][runtime][transform]"
) {
    World world;
    const auto transformed = world.entity();
    world.add_component(transformed, Transform2d {.position = {4.0F, 7.0F}});

    snapshot::SnapshotRegistry registry;
    REQUIRE(snapshot_runtime::configure_builtin_adapters(world, registry));
    CHECK(
        registry.component_policy(type_id<GlobalTransform2d>()) ==
        snapshot::ComponentPolicy::Rebuild
    );

    for (const auto& rebuild : registry.after_restore_hooks()) {
        REQUIRE(rebuild(world));
    }

    REQUIRE(world.has_component<GlobalTransform2d>(transformed));
    const auto& global =
        world.get_component<GlobalTransform2d>(transformed).matrix;
    CHECK(global[0][3] == 4.0F);
    CHECK(global[1][3] == 7.0F);
}

TEST_CASE(
    "Snapshot runtime plugin maintains an automatic checkpoint ring",
    "[snapshot][runtime][automatic]"
) {
    App app;
    app.add_resource(snapshot::CheckpointStore {});
    app.add_resource(
        snapshot_runtime::AutoCheckpointConfig {
            .enabled = true,
            .interval_frames = 1,
            .retain = 2,
            .max_bytes = 1024 * 1024,
            .strict = false,
        }
    );
    app.add_plugin<snapshot_runtime::SnapshotRuntimePlugin>();
    app.update();
    app.update();
    app.update();

    const auto checkpoints = app.resource<snapshot::CheckpointStore>().list();
    REQUIRE(checkpoints.size() == 2);
    CHECK(checkpoints[0].name == "auto-2");
    CHECK(checkpoints[1].name == "auto-3");
}
