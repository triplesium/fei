#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <type_traits>
#include <utility>

using namespace fei;
using namespace fei::ecs_test;

static_assert(std::is_same_v<
              decltype(std::declval<World&>().resource<GameConfig>()),
              GameConfig&>);
static_assert(std::is_same_v<
              decltype(std::declval<const World&>().resource<GameConfig>()),
              const GameConfig&>);

TEST_CASE("ECS manages world resources", "[ecs][resource]") {
    Registry::instance().register_type<GameConfig>();
    Registry::instance().register_type<EventQueue>();

    World world;

    SECTION("Add and get resource") {
        GameConfig config;
        config.max_entities = 500;
        config.dt = 0.02f;

        world.add_resource(config);

        GameConfig& retrieved = world.resource<GameConfig>();
        REQUIRE(retrieved.max_entities == 500);
        REQUIRE(retrieved.dt == 0.02f);
    }

    SECTION("Modify resource") {
        world.add_resource(EventQueue {});

        EventQueue& event_queue = world.resource<EventQueue>();
        event_queue.push("test_event");
        event_queue.push("another_event");

        REQUIRE(event_queue.events.size() == 2);
        REQUIRE(event_queue.events[0] == "test_event");
        REQUIRE(event_queue.events[1] == "another_event");
    }
}

TEST_CASE(
    "ECS systems mutate queried components and resources",
    "[ecs][system]"
) {
    register_components();
    Registry::instance().register_type<EventQueue>();

    World world;
    world.add_resource(EventQueue {});

    Entity entity1 = world.entity();
    world.add_component(entity1, Position(0.0f, 0.0f));
    world.add_component(entity1, Velocity(1.0f, 2.0f));

    Entity entity2 = world.entity();
    world.add_component(entity2, Position(10.0f, 20.0f));
    world.add_component(entity2, Velocity(-1.0f, -2.0f));

    SECTION("Run system once") {
        bool system_executed = false;

        world.run_system_once(
            [&system_executed](Query<Entity, Position, Velocity> query) {
                system_executed = true;
                for (auto [entity, pos, vel] : query) {
                    (void)entity;
                    pos.x += vel.dx;
                    pos.y += vel.dy;
                }
            }
        );

        REQUIRE(system_executed);
        REQUIRE(world.get_component<Position>(entity1) == Position(1.0f, 2.0f));
        REQUIRE(
            world.get_component<Position>(entity2) == Position(9.0f, 18.0f)
        );
    }

    SECTION("System with resource access") {
        world.run_system_once([](Query<Entity> query,
                                 ResRW<EventQueue> events) {
            events->push("system_executed");

            int entity_count = 0;
            for (auto [entity] : query) {
                (void)entity;
                ++entity_count;
            }
            events->push(
                "processed_" + std::to_string(entity_count) + "_entities"
            );
        });

        EventQueue& events = world.resource<EventQueue>();
        REQUIRE(events.events.size() == 2);
        REQUIRE(events.events[0] == "system_executed");
        REQUIRE(events.events[1] == "processed_2_entities");
    }
}

TEST_CASE("ECS optional resource params can be absent", "[ecs][resource]") {
    Registry::instance().register_type<GameConfig>();
    Registry::instance().register_type<EventQueue>();

    World world;

    bool missing_system_ran = false;
    world.run_system_once([&missing_system_ran](
                              Optional<ResRO<GameConfig>> config,
                              Optional<ResRW<EventQueue>> events
                          ) {
        missing_system_ran = true;
        REQUIRE_FALSE(config);
        REQUIRE_FALSE(events);
    });

    REQUIRE(missing_system_ran);

    GameConfig config;
    config.max_entities = 256;
    world.add_resource(config);
    world.add_resource(EventQueue {});

    world.run_system_once([](Optional<ResRO<GameConfig>> config,
                             Optional<ResRW<EventQueue>> events) {
        REQUIRE(config);
        REQUIRE((*config)->max_entities == 256);
        REQUIRE(events);
        (*events)->push("optional_resource_present");
    });

    REQUIRE(world.resource<EventQueue>().events.size() == 1);
    REQUIRE(
        world.resource<EventQueue>().events[0] == "optional_resource_present"
    );
}

TEST_CASE("ECS commands defer entity and component edits", "[ecs][commands]") {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Name>();
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});

    SECTION("Spawn entity with commands") {
        Entity spawned_entity;

        world.run_system_once([&spawned_entity](Commands commands) {
            spawned_entity = commands.spawn()
                                 .add(Position(5.0f, 10.0f))
                                 .add(Name("TestEntity"))
                                 .id();
        });

        world.resource<CommandsQueue>().execute(world);

        REQUIRE(world.has_entity(spawned_entity));
        REQUIRE(world.has_component<Position>(spawned_entity));
        REQUIRE(world.has_component<Name>(spawned_entity));
        REQUIRE(
            world.get_component<Position>(spawned_entity) ==
            Position(5.0f, 10.0f)
        );
        REQUIRE(
            world.get_component<Name>(spawned_entity).value == "TestEntity"
        );
    }

    SECTION("Add component via commands") {
        Entity entity = world.entity();
        world.add_component(entity, Position(0.0f, 0.0f));

        world.run_system_once([entity](Commands commands) {
            commands.entity(entity).add(Name("AddedLater"));
        });

        world.resource<CommandsQueue>().execute(world);

        REQUIRE(world.has_component<Name>(entity));
        REQUIRE(world.get_component<Name>(entity).value == "AddedLater");
    }

    SECTION("Schedule flushes commands between ordered systems") {
        world.add_resource(ScheduleTrace {});

        world.add_systems(
            TestSchedule,
            chain(scheduled_spawn_position, scheduled_count_positions)
        );
        world.sort_systems();
        world.run_schedule(TestSchedule);

        std::vector<std::string> expected = {"count:1"};
        REQUIRE(world.resource<ScheduleTrace>().entries == expected);
    }
}

TEST_CASE(
    "ECS systems compose in a multi-step world scenario",
    "[ecs][integration]"
) {
    register_components();
    Registry::instance().register_type<GameConfig>();
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(GameConfig {});
    world.add_resource(CommandsQueue {});

    Entity player = world.entity();
    world.add_component(player, Position(0.0f, 0.0f));
    world.add_component(player, Velocity(0.0f, 0.0f));
    world.add_component(player, Health(100));
    world.add_component(player, Name("Player"));

    Entity enemy1 = world.entity();
    world.add_component(enemy1, Position(10.0f, 10.0f));
    world.add_component(enemy1, Velocity(-1.0f, 0.0f));
    world.add_component(enemy1, Health(50));
    world.add_component(enemy1, Name("Enemy1"));

    world.run_system_once([](Query<Position, Velocity> query) {
        for (auto [pos, vel] : query) {
            pos.x += vel.dx;
            pos.y += vel.dy;
        }
    });

    world.run_system_once([](Commands commands, ResRW<GameConfig> config) {
        (void)config;
        commands.spawn()
            .add(Position(20.0f, 20.0f))
            .add(Velocity(-0.5f, -0.5f))
            .add(Health(25))
            .add(Name("Enemy2"));
    });

    world.resource<CommandsQueue>().execute(world);

    REQUIRE(world.get_component<Position>(player) == Position(0.0f, 0.0f));
    REQUIRE(world.get_component<Position>(enemy1) == Position(9.0f, 10.0f));

    int entity_count = 0;
    world.run_system_once([&entity_count](Query<Entity, Health> query) {
        for (auto [entity, health] : query) {
            (void)entity;
            (void)health;
            ++entity_count;
        }
    });

    REQUIRE(entity_count == 3);
}
