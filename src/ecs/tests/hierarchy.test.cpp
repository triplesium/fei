#include "ecs/commands.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

TEST_CASE("ECS hierarchy tracks parent and children components", "[ecs][hierarchy]") {
    World world;
    Entity parent = world.entity();
    Entity child = world.entity();

    world.set_parent(child, parent);

    REQUIRE(world.has_parent(child));
    auto parent_entity = world.parent(child);
    REQUIRE(parent_entity);
    REQUIRE(*parent_entity == parent);
    REQUIRE(world.has_component<ChildOf>(child));
    REQUIRE(world.get_component<ChildOf>(child).parent == parent);
    REQUIRE(world.has_component<Children>(parent));

    const auto& children = world.get_component<Children>(parent);
    REQUIRE(children.size() == 1);
    REQUIRE(children.contains(child));

    world.set_parent(child, parent);
    REQUIRE(world.get_component<Children>(parent).size() == 1);
}

TEST_CASE("ECS hierarchy reparents entities", "[ecs][hierarchy]") {
    World world;
    Entity old_parent = world.entity();
    Entity new_parent = world.entity();
    Entity child = world.entity();

    world.set_parent(child, old_parent);
    world.set_parent(child, new_parent);

    auto parent_entity = world.parent(child);
    REQUIRE(parent_entity);
    REQUIRE(*parent_entity == new_parent);
    REQUIRE(!world.has_component<Children>(old_parent));
    REQUIRE(world.has_component<Children>(new_parent));
    REQUIRE(world.get_component<Children>(new_parent).contains(child));
}

TEST_CASE("ECS hierarchy removes parent relationships", "[ecs][hierarchy]") {
    World world;
    Entity parent = world.entity();
    Entity child = world.entity();

    world.set_parent(child, parent);
    world.remove_parent(child);

    REQUIRE(!world.has_parent(child));
    REQUIRE(world.parent(child) == nullopt);
    REQUIRE(!world.has_component<ChildOf>(child));
    REQUIRE(!world.has_component<Children>(parent));

    world.remove_parent(child);
    REQUIRE(!world.has_parent(child));
}

TEST_CASE("ECS hierarchy component APIs stay synchronized", "[ecs][hierarchy]") {
    World world;
    Entity parent = world.entity();
    Entity child = world.entity();

    world.add_component(child, ChildOf {.parent = parent});

    REQUIRE(world.has_parent(child));
    auto parent_entity = world.parent(child);
    REQUIRE(parent_entity);
    REQUIRE(*parent_entity == parent);
    REQUIRE(world.has_component<Children>(parent));
    REQUIRE(world.get_component<Children>(parent).contains(child));

    world.remove_component<ChildOf>(child);

    REQUIRE(!world.has_parent(child));
    REQUIRE(!world.has_component<Children>(parent));
}

TEST_CASE("ECS hierarchy despawns descendants recursively", "[ecs][hierarchy]") {
    World world;
    Entity root = world.entity();
    Entity child = world.entity();
    Entity grandchild = world.entity();
    Entity sibling = world.entity();

    world.set_parent(child, root);
    world.set_parent(grandchild, child);

    world.despawn(root);

    REQUIRE(!world.has_entity(root));
    REQUIRE(!world.has_entity(child));
    REQUIRE(!world.has_entity(grandchild));
    REQUIRE(world.has_entity(sibling));
}

TEST_CASE("ECS hierarchy despawning a child updates the parent", "[ecs][hierarchy]") {
    World world;
    Entity parent = world.entity();
    Entity child_a = world.entity();
    Entity child_b = world.entity();

    world.set_parent(child_a, parent);
    world.set_parent(child_b, parent);

    world.despawn(child_a);

    REQUIRE(world.has_entity(parent));
    REQUIRE(!world.has_entity(child_a));
    REQUIRE(world.has_entity(child_b));
    REQUIRE(world.has_component<Children>(parent));
    const auto& children = world.get_component<Children>(parent);
    REQUIRE(children.size() == 1);
    REQUIRE(children.contains(child_b));

    world.despawn(child_b);
    REQUIRE(!world.has_component<Children>(parent));
}

TEST_CASE("ECS hierarchy commands apply after queue execution", "[ecs][hierarchy]") {
    World world;
    world.add_resource(CommandsQueue {});
    Entity parent = world.entity();
    Entity child = world.entity();

    world.run_system_once([parent, child](Commands commands) {
        commands.entity(child).set_parent(parent);
    });

    REQUIRE(!world.has_parent(child));

    world.resource<CommandsQueue>().execute(world);

    REQUIRE(world.has_parent(child));
    auto parent_entity = world.parent(child);
    REQUIRE(parent_entity);
    REQUIRE(*parent_entity == parent);
    REQUIRE(world.get_component<Children>(parent).contains(child));

    world.run_system_once([child](Commands commands) {
        commands.entity(child).remove_parent();
    });
    world.resource<CommandsQueue>().execute(world);

    REQUIRE(!world.has_parent(child));
    REQUIRE(!world.has_component<Children>(parent));
}

TEST_CASE("ECS hierarchy commands despawn recursively", "[ecs][hierarchy]") {
    World world;
    world.add_resource(CommandsQueue {});
    Entity root = world.entity();
    Entity child = world.entity();

    world.set_parent(child, root);

    world.run_system_once([root](Commands commands) {
        commands.entity(root).despawn_recursive();
    });
    world.resource<CommandsQueue>().execute(world);

    REQUIRE(!world.has_entity(root));
    REQUIRE(!world.has_entity(child));
}
