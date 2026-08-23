#include "ecs/removed_components.hpp"

#include "ecs/commands.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace ets;
using namespace ets::ecs_test;

namespace {

struct RemovalTrace {
    std::vector<Entity> entities;
};

void record_removed_positions(
    RemovedComponents<Position> removed,
    ResRW<RemovalTrace> trace
) {
    while (auto entity = removed.next()) {
        trace->entities.push_back(*entity);
    }
}

void record_removed_velocities(
    RemovedComponents<Velocity> removed,
    ResRW<RemovalTrace> trace
) {
    while (auto entity = removed.next()) {
        trace->entities.push_back(*entity);
    }
}

World removal_world() {
    register_components();
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<RemovalTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(RemovalTrace {});
    return world;
}

} // namespace

TEST_CASE(
    "RemovedComponents reports a component removal once per system",
    "[ecs][change-detection][removed]"
) {
    auto world = removal_world();
    auto entity = world.entity();
    world.add_component(entity, Position {});
    world.add_systems(TestSchedule, record_removed_positions);

    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<RemovalTrace>().entities.empty());

    world.remove_component<Position>(entity);
    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<RemovalTrace>().entities == std::vector {entity});

    world.resource<RemovalTrace>().entities.clear();
    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<RemovalTrace>().entities.empty());
}

TEST_CASE(
    "RemovedComponents readers have independent cursors",
    "[ecs][change-detection][removed]"
) {
    auto world = removal_world();
    auto entity = world.entity();
    world.add_component(entity, Position {});
    world.add_systems(
        TestSchedule,
        record_removed_positions,
        record_removed_positions
    );

    world.remove_component<Position>(entity);
    world.run_schedule(TestSchedule);

    auto entities = world.resource<RemovalTrace>().entities;
    REQUIRE(entities.size() == 2);
    REQUIRE(entities[0] == entity);
    REQUIRE(entities[1] == entity);
}

TEST_CASE(
    "RemovedComponents reports every component held by a despawned entity",
    "[ecs][change-detection][removed][despawn]"
) {
    auto world = removal_world();
    auto entity = world.entity();
    world.add_component(entity, Position {});
    world.add_component(entity, Velocity {});
    world.add_systems(
        TestSchedule,
        record_removed_positions,
        record_removed_velocities
    );

    world.despawn(entity);
    world.run_schedule(TestSchedule);

    auto entities = world.resource<RemovalTrace>().entities;
    REQUIRE(entities.size() == 2);
    REQUIRE(entities[0] == entity);
    REQUIRE(entities[1] == entity);
}

TEST_CASE(
    "RemovedComponents retains messages for two tracker updates",
    "[ecs][change-detection][removed]"
) {
    auto world = removal_world();
    auto retained = world.entity();
    world.add_component(retained, Position {});
    world.remove_component<Position>(retained);
    world.clear_trackers();

    world.add_systems(TestSchedule, record_removed_positions);
    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<RemovalTrace>().entities == std::vector {retained});

    auto dropped = world.entity();
    world.add_component(dropped, Position {});
    world.remove_component<Position>(dropped);
    world.clear_trackers();
    world.clear_trackers();

    constexpr ScheduleId LateSchedule = 321;
    world.add_systems(LateSchedule, record_removed_positions);
    world.resource<RemovalTrace>().entities.clear();
    world.run_schedule(LateSchedule);
    REQUIRE(world.resource<RemovalTrace>().entities.empty());
}

TEST_CASE(
    "RemovedComponents observes deferred removals in a later batch",
    "[ecs][change-detection][removed][commands]"
) {
    auto world = removal_world();
    auto entity = world.entity();
    world.add_component(entity, Position {});

    world.add_systems(
        TestSchedule,
        chain(
            [entity](Commands commands) {
                commands.entity(entity).remove<Position>();
            },
            record_removed_positions
        )
    );

    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<RemovalTrace>().entities == std::vector {entity});
}
