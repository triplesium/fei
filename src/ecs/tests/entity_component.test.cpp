#include "test_types.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fei;
using namespace fei::ecs_test;

TEST_CASE("ECS manages entity lifetime", "[ecs][entity]") {
    register_components();
    World world;

    SECTION("Entity creation") {
        Entity entity1 = world.entity();
        Entity entity2 = world.entity();

        REQUIRE(entity1 != entity2);
        REQUIRE(world.has_entity(entity1));
        REQUIRE(world.has_entity(entity2));
    }

    SECTION("Entity despawn") {
        Entity entity = world.entity();
        REQUIRE(world.has_entity(entity));

        world.despawn(entity);
        REQUIRE_FALSE(world.has_entity(entity));
    }

    SECTION("Despawning a non-last entity keeps moved entity locations valid") {
        Entity first = world.entity();
        world.add_component(first, Position(1.0f, 1.0f));
        world.add_component(first, Health(10));

        Entity middle = world.entity();
        world.add_component(middle, Position(2.0f, 2.0f));
        world.add_component(middle, Health(20));

        Entity last = world.entity();
        world.add_component(last, Position(3.0f, 3.0f));
        world.add_component(last, Health(30));

        world.despawn(first);

        REQUIRE_FALSE(world.has_entity(first));
        REQUIRE(world.has_entity(middle));
        REQUIRE(world.has_entity(last));

        REQUIRE(world.get_component<Position>(middle) == Position(2.0f, 2.0f));
        REQUIRE(world.get_component<Health>(middle) == Health(20));
        REQUIRE(world.get_component<Position>(last) == Position(3.0f, 3.0f));
        REQUIRE(world.get_component<Health>(last) == Health(30));

        std::vector<Entity> remaining;
        world.run_system_once([&remaining](Query<Entity, Position> query) {
            for (auto [entity, pos] : query) {
                (void)pos;
                remaining.push_back(entity);
            }
        });

        std::sort(remaining.begin(), remaining.end());
        std::vector<Entity> expected = {middle, last};
        std::sort(expected.begin(), expected.end());
        REQUIRE(remaining == expected);
    }
}

TEST_CASE("ECS manages components on entities", "[ecs][component]") {
    register_components();
    World world;
    Entity entity = world.entity();

    SECTION("Add component") {
        Position pos(10.0f, 20.0f);
        world.add_component(entity, pos);

        REQUIRE(world.has_component<Position>(entity));
        REQUIRE(world.get_component<Position>(entity) == pos);
    }

    SECTION("Add multiple components") {
        Position pos(5.0f, 15.0f);
        Velocity vel(2.0f, 3.0f);
        Health hp(75);

        world.add_component(entity, pos);
        world.add_component(entity, vel);
        world.add_component(entity, hp);

        REQUIRE(world.has_component<Position>(entity));
        REQUIRE(world.has_component<Velocity>(entity));
        REQUIRE(world.has_component<Health>(entity));

        REQUIRE(world.get_component<Position>(entity) == pos);
        REQUIRE(world.get_component<Velocity>(entity) == vel);
        REQUIRE(world.get_component<Health>(entity) == hp);
    }

    SECTION("Remove component") {
        world.add_component(entity, Position(1.0f, 2.0f));
        world.add_component(entity, Velocity(3.0f, 4.0f));

        world.remove_component<Position>(entity);

        REQUIRE_FALSE(world.has_component<Position>(entity));
        REQUIRE(world.has_component<Velocity>(entity));
    }

    SECTION("Component modification") {
        world.add_component(entity, Position(0.0f, 0.0f));

        Position& component = world.get_component<Position>(entity);
        component.x = 100.0f;
        component.y = 200.0f;

        REQUIRE(world.get_component<Position>(entity).x == 100.0f);
        REQUIRE(world.get_component<Position>(entity).y == 200.0f);
    }
}

TEST_CASE(
    "ECS archetypes retain component rows across moves",
    "[ecs][archetype]"
) {
    register_components();
    World world;

    SECTION("Archetype changes on component addition") {
        Entity entity = world.entity();

        world.add_component(entity, Position(1.0f, 2.0f));
        world.add_component(entity, Velocity(3.0f, 4.0f));

        REQUIRE(world.has_component<Position>(entity));
        REQUIRE(world.has_component<Velocity>(entity));
        REQUIRE(world.get_component<Position>(entity) == Position(1.0f, 2.0f));
        REQUIRE(world.get_component<Velocity>(entity) == Velocity(3.0f, 4.0f));
    }

    SECTION("Archetype changes on component removal") {
        Entity entity = world.entity();

        world.add_component(entity, Position(5.0f, 6.0f));
        world.add_component(entity, Velocity(7.0f, 8.0f));
        world.add_component(entity, Health(100));

        world.remove_component<Velocity>(entity);

        REQUIRE(world.has_component<Position>(entity));
        REQUIRE_FALSE(world.has_component<Velocity>(entity));
        REQUIRE(world.has_component<Health>(entity));
        REQUIRE(world.get_component<Position>(entity) == Position(5.0f, 6.0f));
        REQUIRE(world.get_component<Health>(entity) == Health(100));
    }

    SECTION("Removing a component from a non-last entity keeps moved rows valid"
    ) {
        Entity first = world.entity();
        world.add_component(first, Position(1.0f, 1.0f));
        world.add_component(first, Velocity(10.0f, 10.0f));
        world.add_component(first, Health(10));

        Entity middle = world.entity();
        world.add_component(middle, Position(2.0f, 2.0f));
        world.add_component(middle, Velocity(20.0f, 20.0f));
        world.add_component(middle, Health(20));

        Entity last = world.entity();
        world.add_component(last, Position(3.0f, 3.0f));
        world.add_component(last, Velocity(30.0f, 30.0f));
        world.add_component(last, Health(30));

        world.remove_component<Velocity>(first);

        REQUIRE(world.has_component<Position>(first));
        REQUIRE_FALSE(world.has_component<Velocity>(first));
        REQUIRE(world.has_component<Health>(first));
        REQUIRE(world.get_component<Position>(first) == Position(1.0f, 1.0f));
        REQUIRE(world.get_component<Health>(first) == Health(10));

        REQUIRE(world.has_component<Velocity>(middle));
        REQUIRE(world.get_component<Position>(middle) == Position(2.0f, 2.0f));
        REQUIRE(
            world.get_component<Velocity>(middle) == Velocity(20.0f, 20.0f)
        );
        REQUIRE(world.get_component<Health>(middle) == Health(20));

        REQUIRE(world.has_component<Velocity>(last));
        REQUIRE(world.get_component<Position>(last) == Position(3.0f, 3.0f));
        REQUIRE(world.get_component<Velocity>(last) == Velocity(30.0f, 30.0f));
        REQUIRE(world.get_component<Health>(last) == Health(30));
    }
}
