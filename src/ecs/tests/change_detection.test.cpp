#include "ecs/dynamic/query.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace fei;
using namespace fei::ecs_test;

namespace {

using WritablePositionResult =
    std::tuple_element_t<0, Query<Position>::Iterator::value_type>;

static_assert(std::same_as<WritablePositionResult, ComponentRW<Position>>);

void prepare_change_detection_world(World& world) {
    register_components();
    Registry::instance().register_type<CommandsQueue>();
    world.add_resource(CommandsQueue {});
}

} // namespace

TEST_CASE(
    "ECS detects added and changed components per system",
    "[ecs][change_detection]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity entity = world.entity();
    world.add_component(entity, Position(1.0f, 2.0f));

    std::vector<std::size_t> added_counts;
    std::vector<std::size_t> changed_counts;
    world.add_systems(
        TestSchedule,
        [&added_counts](
            Query<Entity, const Position>::Filter<Added<Position>> query
        ) {
            added_counts.push_back(query.size());
        },
        [&changed_counts](
            Query<Entity, const Position>::Filter<Changed<Position>> query
        ) {
            changed_counts.push_back(query.size());
        }
    );

    world.run_schedule(TestSchedule);
    world.run_schedule(TestSchedule);

    auto position = world.get_component_rw<Position>(entity);
    position->x = 10.0f;
    world.run_schedule(TestSchedule);

    REQUIRE(added_counts == std::vector<std::size_t> {1, 0, 0});
    REQUIRE(changed_counts == std::vector<std::size_t> {1, 0, 1});
}

TEST_CASE(
    "ECS retained world write handles use a fresh tick per mutation",
    "[ecs][change_detection][world]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity entity = world.entity();
    world.add_component(entity, Position(1.0f, 2.0f));

    std::vector<std::size_t> changed_counts;
    world.add_systems(
        TestSchedule,
        [&changed_counts](
            Query<const Position>::Filter<Changed<Position>> query
        ) {
            changed_counts.push_back(query.size());
        }
    );
    world.run_schedule(TestSchedule);

    auto position = world.get_component_rw<Position>(entity);
    position->x = 3.0f;
    world.run_schedule(TestSchedule);

    position->y = 4.0f;
    world.run_schedule(TestSchedule);

    REQUIRE(changed_counts == std::vector<std::size_t> {1, 1, 1});
    REQUIRE(world.get_component<Position>(entity) == Position(3.0f, 4.0f));
}

TEST_CASE(
    "ECS mutable dynamic lookup returns empty for a missing component",
    "[ecs][change_detection][world]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity entity = world.entity();
    world.add_component(entity, Position(1.0f, 2.0f));

    auto missing = world.get_component(entity, type_id<Velocity>());

    REQUIRE_FALSE(missing);
}

TEST_CASE(
    "ECS writable component proxies mark only mutable access",
    "[ecs][change_detection]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity entity = world.entity();
    world.add_component(entity, Position(1.0f, 2.0f));

    bool write = false;
    std::vector<std::size_t> changed_counts;
    auto maybe_write = [&write](Query<Position> query) {
        for (auto [position] : query) {
            if (write) {
                position->x += 1.0f;
            } else {
                (void)position;
            }
        }
    };
    auto detect = [&changed_counts](
                      Query<const Position>::Filter<Changed<Position>> query
                  ) {
        changed_counts.push_back(query.size());
    };

    world.add_systems(
        TestSchedule,
        chain(std::move(maybe_write), std::move(detect))
    );
    world.run_schedule(TestSchedule);
    world.run_schedule(TestSchedule);

    write = true;
    world.run_schedule(TestSchedule);

    REQUIRE(changed_counts == std::vector<std::size_t> {1, 0, 1});
    REQUIRE(world.get_component<Position>(entity) == Position(2.0f, 2.0f));
}

TEST_CASE(
    "ECS preserves component ticks across archetype moves",
    "[ecs][change_detection][archetype]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity entity = world.entity();
    world.add_component(entity, Position(1.0f, 2.0f));

    std::vector<std::size_t> added_counts;
    std::vector<std::size_t> changed_counts;
    world.add_systems(
        TestSchedule,
        [&added_counts](
            Query<Entity, const Position>::Filter<Added<Position>> query
        ) {
            added_counts.push_back(query.size());
        },
        [&changed_counts](
            Query<Entity, const Position>::Filter<Changed<Position>> query
        ) {
            changed_counts.push_back(query.size());
        }
    );

    world.run_schedule(TestSchedule);

    world.add_component(entity, Velocity(3.0f, 4.0f));
    world.run_schedule(TestSchedule);

    world.remove_component<Velocity>(entity);
    world.run_schedule(TestSchedule);

    world.add_component(entity, Position(1.0f, 2.0f));
    world.run_schedule(TestSchedule);

    world.remove_component<Position>(entity);
    world.add_component(entity, Position(5.0f, 6.0f));
    world.run_schedule(TestSchedule);

    REQUIRE(added_counts == std::vector<std::size_t> {1, 0, 0, 0, 1});
    REQUIRE(changed_counts == std::vector<std::size_t> {1, 0, 0, 1, 1});
}

TEST_CASE(
    "ECS keeps changes until a gated system runs",
    "[ecs][change_detection][schedule]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity entity = world.entity();
    world.add_component(entity, Position(1.0f, 2.0f));

    bool enabled = true;
    std::vector<std::size_t> changed_counts;
    auto detector = [&changed_counts](
                        Query<const Position>::Filter<Changed<Position>> query
                    ) {
        changed_counts.push_back(query.size());
    };
    world.add_systems(TestSchedule, std::move(detector) | run_if([&enabled]() {
                                        return enabled;
                                    }));

    world.run_schedule(TestSchedule);

    enabled = false;
    auto position = world.get_component_rw<Position>(entity);
    position->y = 8.0f;
    world.run_schedule(TestSchedule);

    enabled = true;
    world.run_schedule(TestSchedule);

    REQUIRE(changed_counts == std::vector<std::size_t> {1, 1});
}

TEST_CASE(
    "ECS keeps change ticks aligned during swap removal",
    "[ecs][change_detection][archetype]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity first = world.entity();
    world.add_component(first, Position(1.0f, 1.0f));
    Entity last = world.entity();
    world.add_component(last, Position(2.0f, 2.0f));

    std::vector<std::vector<Entity>> changed_entities;
    world.add_systems(
        TestSchedule,
        [&changed_entities](
            Query<Entity, const Position>::Filter<Changed<Position>> query
        ) {
            std::vector<Entity> entities;
            for (auto [entity, position] : query) {
                (void)position;
                entities.push_back(entity);
            }
            changed_entities.push_back(std::move(entities));
        }
    );

    world.run_schedule(TestSchedule);

    auto position = world.get_component_rw<Position>(last);
    position->x = 3.0f;
    world.remove_component<Position>(first);
    world.run_schedule(TestSchedule);

    REQUIRE(changed_entities.size() == 2);
    REQUIRE(changed_entities[1] == std::vector<Entity> {last});
}

TEST_CASE(
    "ECS detects resource changes through resource proxies",
    "[ecs][change_detection][resource]"
) {
    World world;
    prepare_change_detection_world(world);
    Registry::instance().register_type<GameConfig>();
    world.add_resource(GameConfig {});

    bool write = false;
    std::vector<std::pair<bool, bool>> detections;
    auto maybe_write = [&write](ResRW<GameConfig> config) {
        if (write) {
            config->max_entities += 1;
        } else {
            (void)config;
        }
    };
    auto detect = [&detections](ResRO<GameConfig> config) {
        detections.emplace_back(config.is_added(), config.is_changed());
    };
    world.add_systems(
        TestSchedule,
        chain(std::move(maybe_write), std::move(detect))
    );

    world.run_schedule(TestSchedule);
    world.run_schedule(TestSchedule);

    write = true;
    world.run_schedule(TestSchedule);

    world.add_resource(GameConfig {.max_entities = 10});
    world.run_schedule(TestSchedule);

    REQUIRE(
        detections == std::vector<std::pair<bool, bool>> {
                          {true, true},
                          {false, false},
                          {false, true},
                          {false, true},
                      }
    );
}

TEST_CASE(
    "ECS detects writes made through dynamic queries",
    "[ecs][change_detection][dynamic]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity entity = world.entity();
    world.add_component(entity, Position(1.0f, 2.0f));

    std::vector<std::size_t> changed_counts;
    world.add_systems(
        TestSchedule,
        [&changed_counts](
            Query<const Position>::Filter<Changed<Position>> query
        ) {
            changed_counts.push_back(query.size());
        }
    );
    world.run_schedule(TestSchedule);

    DynamicQuery query(
        "positions",
        {
            DynamicQueryField {
                .name = "position",
                .type = type_id<Position>(),
                .access = DynamicParamAccess::Write,
            },
        },
        {}
    );
    REQUIRE(query.prepare(world));

    DynamicQueryCursor cursor;
    DynamicQueryRow row;
    REQUIRE(query.next(cursor, row));
    query.field(row, 0).get<Position>().x = 5.0f;

    world.run_schedule(TestSchedule);

    REQUIRE(changed_counts == std::vector<std::size_t> {1, 1});
    REQUIRE(world.get_component<Position>(entity) == Position(5.0f, 2.0f));
}

TEST_CASE(
    "ECS combines change filters with OR",
    "[ecs][change_detection][query]"
) {
    World world;
    prepare_change_detection_world(world);

    Entity position_entity = world.entity();
    world.add_component(position_entity, Position(1.0f, 2.0f));
    Entity velocity_entity = world.entity();
    world.add_component(velocity_entity, Velocity(3.0f, 4.0f));

    std::vector<std::size_t> changed_counts;
    world.add_systems(
        TestSchedule,
        [&changed_counts](
            Query<Entity>::Filter<Or<Changed<Position>, Changed<Velocity>>>
                query
        ) {
            changed_counts.push_back(query.size());
        }
    );

    world.run_schedule(TestSchedule);
    world.run_schedule(TestSchedule);

    world.get_component_rw<Position>(position_entity)->x = 5.0f;
    world.get_component_rw<Velocity>(velocity_entity)->dx = 6.0f;
    world.run_schedule(TestSchedule);

    REQUIRE(changed_counts == std::vector<std::size_t> {2, 0, 2});
}
