#include "ecs/schedule.hpp"
#include "test_types.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::ecs_test;

namespace {

struct ScheduleMergeFirstSet : SystemSet<ScheduleMergeFirstSet> {};
struct ScheduleMergeSecondSet : SystemSet<ScheduleMergeSecondSet> {};
struct ScheduleMergeThirdSet : SystemSet<ScheduleMergeThirdSet> {};
struct ScheduleInstallSet : SystemSet<ScheduleInstallSet> {};
struct ScheduleInstalledSet : SystemSet<ScheduleInstalledSet> {};
struct RunIfCounter {
    int calls = 0;
};

void schedule_merge_first() {}

void schedule_merge_second() {}

void schedule_merge_third() {}

void scheduled_replacement(ResRW<ScheduleTrace> trace) {
    trace->entries.emplace_back("replacement");
}

void scheduled_install_second(Commands commands, ResRW<ScheduleTrace> trace) {
    trace->entries.emplace_back("install");
    commands.add_system(
        TestSchedule,
        scheduled_second | in_set<ScheduleInstalledSet>()
    );
}

void condition_read_position(Query<const Position>) {}

void condition_write_config(ResRW<GameConfig>) {}

void increment_run_if_counter(ResRW<RunIfCounter> counter) {
    ++counter->calls;
}

} // namespace

TEST_CASE("ECS queries select matching component sets", "[ecs][query]") {
    register_components();
    World world;

    Entity entity1 = world.entity();
    world.add_component(entity1, Position(1.0f, 1.0f));
    world.add_component(entity1, Velocity(0.1f, 0.1f));

    Entity entity2 = world.entity();
    world.add_component(entity2, Position(2.0f, 2.0f));
    world.add_component(entity2, Health(50));

    Entity entity3 = world.entity();
    world.add_component(entity3, Position(3.0f, 3.0f));
    world.add_component(entity3, Velocity(0.3f, 0.3f));
    world.add_component(entity3, Health(75));

    Entity entity4 = world.entity();
    world.add_component(entity4, Name("OnlyName"));

    SECTION("Query with single component") {
        std::vector<Entity> entities_with_position;

        world.run_system_once(
            [&entities_with_position](Query<Entity, Position> query) {
                for (auto [entity, pos] : query) {
                    (void)pos;
                    entities_with_position.push_back(entity);
                }
            }
        );

        std::sort(entities_with_position.begin(), entities_with_position.end());
        std::vector<Entity> expected = {entity1, entity2, entity3};
        std::sort(expected.begin(), expected.end());
        REQUIRE(entities_with_position == expected);
    }

    SECTION("Query with multiple components") {
        std::vector<Entity> entities_with_pos_and_vel;

        world.run_system_once([&entities_with_pos_and_vel](
                                  Query<Entity, Position, Velocity> query
                              ) {
            for (auto [entity, pos, vel] : query) {
                (void)pos;
                (void)vel;
                entities_with_pos_and_vel.push_back(entity);
            }
        });

        std::sort(
            entities_with_pos_and_vel.begin(),
            entities_with_pos_and_vel.end()
        );
        std::vector<Entity> expected = {entity1, entity3};
        std::sort(expected.begin(), expected.end());
        REQUIRE(entities_with_pos_and_vel == expected);
    }

    SECTION("Query with all three components") {
        std::vector<Entity> entities_with_all_three;

        world.run_system_once(
            [&entities_with_all_three](
                Query<Entity, Position, Velocity, Health> query
            ) {
                for (auto [entity, pos, vel, health] : query) {
                    (void)pos;
                    (void)vel;
                    (void)health;
                    entities_with_all_three.push_back(entity);
                }
            }
        );

        REQUIRE(entities_with_all_three.size() == 1);
        REQUIRE(entities_with_all_three[0] == entity3);
    }

    SECTION("Empty query") {
        int count = 0;

        world.run_system_once([&count](Query<Entity, Position, Name> query) {
            for (auto [entity, pos, name] : query) {
                (void)entity;
                (void)pos;
                (void)name;
                ++count;
            }
        });

        REQUIRE(count == 0);
    }
}

TEST_CASE("ECS queries access matching entities by id", "[ecs][query]") {
    register_components();
    World world;

    Entity matching = world.entity();
    world.add_component(matching, Position(1.0f, 2.0f));
    world.add_component(matching, Velocity(3.0f, 4.0f));
    world.add_component(matching, Health(50));

    Entity filtered_out = world.entity();
    world.add_component(filtered_out, Position(3.0f, 4.0f));
    world.add_component(filtered_out, Velocity(5.0f, 6.0f));

    Entity missing_velocity = world.entity();
    world.add_component(missing_velocity, Position(5.0f, 6.0f));

    Entity despawned = world.entity();
    world.add_component(despawned, Position(7.0f, 8.0f));
    world.add_component(despawned, Velocity(9.0f, 10.0f));
    world.despawn(despawned);

    world.run_system_once(
        [&](
            Query<Entity, Position, const Velocity>::Filter<With<Health>> query
        ) {
            auto item = query.get(matching);
            REQUIRE(item);
            auto [entity, position, velocity] = *item;
            CHECK(entity == matching);
            CHECK(position->x == 1.0f);
            CHECK(velocity.dx == 3.0f);

            position->x = 11.0f;

            CHECK_FALSE(query.get(filtered_out));
            CHECK_FALSE(query.get(missing_velocity));
            CHECK_FALSE(query.get(despawned));
        }
    );

    CHECK(world.get_component<Position>(matching).x == 11.0f);
}

TEST_CASE("ECS schedule ordering respects configured sets", "[ecs][schedule]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    world.configure_sets(
        TestSchedule,
        chain(ScheduleFirstSet {}, ScheduleSecondSet {})
    );
    world.add_systems(
        TestSchedule,
        scheduled_second | in_set<ScheduleSecondSet>(),
        scheduled_first | in_set<ScheduleFirstSet>()
    );
    world.sort_systems();
    world.run_schedule(TestSchedule);

    std::vector<std::string> expected = {"first", "second"};
    REQUIRE(world.resource<ScheduleTrace>().entries == expected);
}

TEST_CASE("ECS schedules run systems added after sorting", "[ecs][schedule]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    world.configure_sets(
        TestSchedule,
        chain(ScheduleFirstSet {}, ScheduleSecondSet {})
    );
    auto handles = world.add_systems(
        TestSchedule,
        scheduled_first | in_set<ScheduleFirstSet>()
    );
    REQUIRE(handles.size() == 1);
    REQUIRE(handles[0].schedule == TestSchedule);

    world.sort_systems();
    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );

    world.resource<ScheduleTrace>().entries.clear();
    auto added_handles = world.add_systems(
        TestSchedule,
        scheduled_second | in_set<ScheduleSecondSet>()
    );
    REQUIRE(added_handles.size() == 1);
    REQUIRE(added_handles[0].schedule == TestSchedule);
    REQUIRE(added_handles[0].id != handles[0].id);

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first", "second"}
    );
}

TEST_CASE("ECS schedules remove systems by handle", "[ecs][schedule]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    auto first_config = SystemConfig(scheduled_first);
    auto second_config = SystemConfig(scheduled_second);
    second_config.after(first_config);
    auto handles = world.add_systems(
        TestSchedule,
        std::move(first_config),
        std::move(second_config)
    );
    REQUIRE(handles.size() == 2);
    REQUIRE(world.remove_system(handles[1]));
    REQUIRE_FALSE(world.remove_system(handles[1]));

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );
}

TEST_CASE("ECS schedules replace systems by handle", "[ecs][schedule]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    auto handle = world.add_system(TestSchedule, SystemConfig(scheduled_first));

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );

    world.resource<ScheduleTrace>().entries.clear();
    REQUIRE(world.replace_system(handle, SystemConfig(scheduled_replacement)));

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"replacement"}
    );

    world.resource<ScheduleTrace>().entries.clear();
    REQUIRE(world.remove_system(handle));

    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<ScheduleTrace>().entries.empty());
}

TEST_CASE(
    "ECS commands keep schedule edits out of after-batch flush",
    "[ecs][commands][schedule]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    world.run_system_once([](Commands commands) {
        commands.add_system(TestSchedule, SystemConfig(scheduled_first));
    });

    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<ScheduleTrace>().entries.empty());

    world.resource<CommandsQueue>().execute_after_schedule(world);
    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );
}

TEST_CASE(
    "ECS commands apply added systems after the current schedule",
    "[ecs][commands][schedule]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    world.configure_sets(
        TestSchedule,
        chain(ScheduleInstallSet {}, ScheduleInstalledSet {})
    );
    world.add_systems(
        TestSchedule,
        scheduled_install_second | in_set<ScheduleInstallSet>()
    );

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"install"}
    );

    world.resource<ScheduleTrace>().entries.clear();
    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"install", "second"}
    );
}

TEST_CASE(
    "ECS commands remove systems at schedule timing",
    "[ecs][commands][schedule]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    auto handle = world.add_system(TestSchedule, SystemConfig(scheduled_first));
    world.run_system_once([handle](Commands commands) {
        commands.remove_system(handle);
    });
    world.resource<CommandsQueue>().execute_after_schedule(world);

    world.run_schedule(TestSchedule);
    REQUIRE(world.resource<ScheduleTrace>().entries.empty());
}

TEST_CASE(
    "ECS commands replace systems at schedule timing",
    "[ecs][commands][schedule]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    auto handle = world.add_system(TestSchedule, SystemConfig(scheduled_first));
    world.run_system_once([handle](Commands commands) {
        commands.replace_system(handle, SystemConfig(scheduled_replacement));
    });
    world.resource<CommandsQueue>().execute_after_schedule(world);

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"replacement"}
    );
}

TEST_CASE("ECS run_if gates scheduled systems", "[ecs][schedule]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();
    Registry::instance().register_type<GameConfig>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});
    world.add_resource(GameConfig {.max_entities = 0});

    world.add_systems(
        TestSchedule,
        chain(
            scheduled_first | run_if([](ResRO<GameConfig> config) {
                return config->max_entities > 0;
            }),
            scheduled_second | run_if([](ResRO<GameConfig> config) {
                return config->max_entities == 0;
            })
        )
    );
    world.sort_systems();

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"second"}
    );

    world.resource<ScheduleTrace>().entries.clear();
    world.resource<GameConfig>().max_entities = 1;

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );
}

TEST_CASE(
    "ECS run_if combines multiple conditions with AND",
    "[ecs][schedule]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<RunIfCounter>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(RunIfCounter {});

    auto always_true = []() {
        return true;
    };
    auto always_false = []() {
        return false;
    };

    world.add_systems(
        TestSchedule,
        increment_run_if_counter | run_if(always_true) | run_if(always_false)
    );
    world.sort_systems();
    world.run_schedule(TestSchedule);

    REQUIRE(world.resource<RunIfCounter>().calls == 0);
}

TEST_CASE("ECS run_if applies to system groups", "[ecs][schedule]") {
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(CommandsQueue {});

    int calls = 0;
    world.add_systems(
        TestSchedule,
        all(
            [&calls]() {
                ++calls;
            },
            [&calls]() {
                ++calls;
            }
        ) | run_if([]() {
            return false;
        })
    );
    world.sort_systems();
    world.run_schedule(TestSchedule);

    REQUIRE(calls == 0);
}

TEST_CASE(
    "ECS run_if access participates in schedule batching",
    "[ecs][schedule]"
) {
    Schedule schedule;
    schedule.add_systems(
        condition_read_position | run_if([](ResRO<GameConfig>) {
            return true;
        }),
        condition_write_config
    );
    schedule.sort_systems();

    REQUIRE(schedule.execution_batches().size() == 2);
    REQUIRE(schedule.execution_batches()[0].size() == 1);
    REQUIRE(schedule.execution_batches()[1].size() == 1);
}

TEST_CASE("ECS run_if has resource helper conditions", "[ecs][schedule]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();
    Registry::instance().register_type<GameConfig>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});

    world.add_systems(
        TestSchedule,
        chain(
            scheduled_first | run_if(resource_missing<GameConfig>()),
            scheduled_second | run_if(resource_exists<GameConfig>())
        )
    );
    world.sort_systems();

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );

    world.resource<ScheduleTrace>().entries.clear();
    world.add_resource(GameConfig {});

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"second"}
    );
}

TEST_CASE("ECS schedule merges repeated set configuration", "[ecs][schedule]") {
    Schedule schedule;

    schedule.configure_sets(
        ScheduleMergeSecondSet {}.after<ScheduleMergeFirstSet>()
    );
    schedule.configure_sets(
        ScheduleMergeSecondSet {}.before<ScheduleMergeThirdSet>()
    );
    schedule.add_systems(
        schedule_merge_first | in_set<ScheduleMergeFirstSet>(),
        schedule_merge_second | in_set<ScheduleMergeSecondSet>(),
        schedule_merge_third | in_set<ScheduleMergeThirdSet>()
    );
    schedule.sort_systems();

    REQUIRE(schedule.execution_batches().size() == 3);
    REQUIRE(schedule.execution_batches()[0].size() == 1);
    REQUIRE(schedule.execution_batches()[1].size() == 1);
    REQUIRE(schedule.execution_batches()[2].size() == 1);
}
