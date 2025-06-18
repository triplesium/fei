#include <catch2/catch_test_macros.hpp>

#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/system.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

using namespace fei;

// Test components for ECS testing
struct Position {
    float x, y;

    Position() : x(0), y(0) {}
    Position(float x, float y) : x(x), y(y) {}

    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }
};

struct Velocity {
    float dx, dy;

    Velocity() : dx(0), dy(0) {}
    Velocity(float dx, float dy) : dx(dx), dy(dy) {}

    bool operator==(const Velocity& other) const {
        return dx == other.dx && dy == other.dy;
    }
};

struct Health {
    int value;

    Health() : value(100) {}
    Health(int value) : value(value) {}

    bool operator==(const Health& other) const { return value == other.value; }
};

struct Name {
    std::string value;

    Name() : value("") {}
    Name(const std::string& value) : value(value) {}

    bool operator==(const Name& other) const { return value == other.value; }
};

// Test resources
struct GameConfig {
    int max_entities = 1000;
    float dt = 0.016f;
};

struct EventQueue {
    std::vector<std::string> events;

    void push(const std::string& event) { events.push_back(event); }
};

// Test events for ECS event system testing
struct PlayerMoved {
    Entity player;
    Position from;
    Position to;

    PlayerMoved() = default;
    PlayerMoved(Entity player, Position from, Position to) :
        player(player), from(from), to(to) {}

    bool operator==(const PlayerMoved& other) const {
        return player == other.player && from == other.from && to == other.to;
    }
};

struct GameEvent {
    std::string message;

    GameEvent() = default;
    GameEvent(const std::string& message) : message(message) {}

    bool operator==(const GameEvent& other) const {
        return message == other.message;
    }
};

TEST_CASE("ECS Entity Management", "[ecs][entity]") {
    // Register test component types
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Velocity>();
    Registry::instance().register_type<Health>();
    Registry::instance().register_type<Name>();

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
}

TEST_CASE("ECS Component Management", "[ecs][component]") {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Velocity>();
    Registry::instance().register_type<Health>();
    Registry::instance().register_type<Name>();

    World world;
    Entity entity = world.entity();

    SECTION("Add component") {
        Position pos(10.0f, 20.0f);
        world.add_component(entity, pos);

        REQUIRE(world.has_component<Position>(entity));
        Position& retrieved = world.get_component<Position>(entity);
        REQUIRE(retrieved == pos);
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
        Position pos(1.0f, 2.0f);
        Velocity vel(3.0f, 4.0f);

        world.add_component(entity, pos);
        world.add_component(entity, vel);

        REQUIRE(world.has_component<Position>(entity));
        REQUIRE(world.has_component<Velocity>(entity));

        world.remove_component<Position>(entity);

        REQUIRE_FALSE(world.has_component<Position>(entity));
        REQUIRE(world.has_component<Velocity>(entity));
    }

    SECTION("Component modification") {
        Position pos(0.0f, 0.0f);
        world.add_component(entity, pos);

        Position& component = world.get_component<Position>(entity);
        component.x = 100.0f;
        component.y = 200.0f;

        Position& retrieved = world.get_component<Position>(entity);
        REQUIRE(retrieved.x == 100.0f);
        REQUIRE(retrieved.y == 200.0f);
    }
}

TEST_CASE("ECS World Resource Management", "[ecs][resource]") {
    Registry::instance().register_type<GameConfig>();
    Registry::instance().register_type<EventQueue>();

    World world;

    SECTION("Add and get resource") {
        GameConfig config;
        config.max_entities = 500;
        config.dt = 0.02f;

        world.add_resource(config);

        GameConfig& retrieved = world.get_resource<GameConfig>();
        REQUIRE(retrieved.max_entities == 500);
        REQUIRE(retrieved.dt == 0.02f);
    }

    SECTION("Modify resource") {
        EventQueue queue;
        world.add_resource(queue);

        EventQueue& event_queue = world.get_resource<EventQueue>();
        event_queue.push("test_event");
        event_queue.push("another_event");

        EventQueue& retrieved = world.get_resource<EventQueue>();
        REQUIRE(retrieved.events.size() == 2);
        REQUIRE(retrieved.events[0] == "test_event");
        REQUIRE(retrieved.events[1] == "another_event");
    }
}

TEST_CASE("ECS System Execution", "[ecs][system]") {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Velocity>();
    Registry::instance().register_type<EventQueue>();

    World world;
    world.add_resource(EventQueue {});

    // Create test entities
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
                    pos.x += vel.dx;
                    pos.y += vel.dy;
                }
            }
        );

        REQUIRE(system_executed);

        // Check that positions were updated
        Position& pos1 = world.get_component<Position>(entity1);
        REQUIRE(pos1.x == 1.0f);
        REQUIRE(pos1.y == 2.0f);

        Position& pos2 = world.get_component<Position>(entity2);
        REQUIRE(pos2.x == 9.0f);
        REQUIRE(pos2.y == 18.0f);
    }

    SECTION("System with resource access") {
        world.run_system_once([](Query<Entity> query, Res<EventQueue> events) {
            events->push("system_executed");

            int entity_count = 0;
            for (auto [entity] : query) {
                (void)entity; // Use entity to avoid warning
                entity_count++;
            }
            events->push(
                "processed_" + std::to_string(entity_count) + "_entities"
            );
        });

        EventQueue& events = world.get_resource<EventQueue>();
        REQUIRE(events.events.size() == 2);
        REQUIRE(events.events[0] == "system_executed");
        REQUIRE(events.events[1] == "processed_2_entities");
    }
}

TEST_CASE("ECS Commands System", "[ecs][commands]") {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Name>();
    Registry::instance().register_type<CommandsQueue>();

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

        // Execute queued commands
        world.get_resource<CommandsQueue>().execute(world);

        REQUIRE(world.has_entity(spawned_entity));
        REQUIRE(world.has_component<Position>(spawned_entity));
        REQUIRE(world.has_component<Name>(spawned_entity));

        Position& pos = world.get_component<Position>(spawned_entity);
        REQUIRE(pos.x == 5.0f);
        REQUIRE(pos.y == 10.0f);

        Name& name = world.get_component<Name>(spawned_entity);
        REQUIRE(name.value == "TestEntity");
    }

    SECTION("Add component via commands") {
        Entity entity = world.entity();
        world.add_component(entity, Position(0.0f, 0.0f));

        world.run_system_once([entity](Commands commands) {
            commands.entity(entity).add(Name("AddedLater"));
        });

        world.get_resource<CommandsQueue>().execute(world);

        REQUIRE(world.has_component<Name>(entity));
        Name& name = world.get_component<Name>(entity);
        REQUIRE(name.value == "AddedLater");
    }
}

TEST_CASE("ECS Query System", "[ecs][query]") {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Velocity>();
    Registry::instance().register_type<Health>();
    Registry::instance().register_type<Name>();

    World world;

    // Create entities with different component combinations
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
                    entities_with_position.push_back(entity);
                }
            }
        );

        REQUIRE(entities_with_position.size() == 3);
        // All entities except entity4 should have Position
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
                entities_with_pos_and_vel.push_back(entity);
            }
        });

        REQUIRE(entities_with_pos_and_vel.size() == 2);
        // Only entity1 and entity3 have both Position and Velocity
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
                (void)name; // Use variables to avoid warnings
                count++;
            }
        });

        REQUIRE(count == 0); // No entity has both Position and Name
    }
}

TEST_CASE("ECS Archetype System", "[ecs][archetype]") {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Velocity>();
    Registry::instance().register_type<Health>();

    World world;

    SECTION("Archetype changes on component addition") {
        Entity entity = world.entity();

        // Add first component
        world.add_component(entity, Position(1.0f, 2.0f));

        // Add second component
        world.add_component(entity, Velocity(3.0f, 4.0f));

        // Verify entity has both components
        REQUIRE(world.has_component<Position>(entity));
        REQUIRE(world.has_component<Velocity>(entity));

        Position& pos = world.get_component<Position>(entity);
        Velocity& vel = world.get_component<Velocity>(entity);

        REQUIRE(pos.x == 1.0f);
        REQUIRE(pos.y == 2.0f);
        REQUIRE(vel.dx == 3.0f);
        REQUIRE(vel.dy == 4.0f);
    }

    SECTION("Archetype changes on component removal") {
        Entity entity = world.entity();

        // Add multiple components
        world.add_component(entity, Position(5.0f, 6.0f));
        world.add_component(entity, Velocity(7.0f, 8.0f));
        world.add_component(entity, Health(100));

        REQUIRE(world.has_component<Position>(entity));
        REQUIRE(world.has_component<Velocity>(entity));
        REQUIRE(world.has_component<Health>(entity));

        // Remove one component
        world.remove_component<Velocity>(entity);

        REQUIRE(world.has_component<Position>(entity));
        REQUIRE_FALSE(world.has_component<Velocity>(entity));
        REQUIRE(world.has_component<Health>(entity));

        // Verify remaining components are still accessible
        Position& pos = world.get_component<Position>(entity);
        Health& health = world.get_component<Health>(entity);

        REQUIRE(pos.x == 5.0f);
        REQUIRE(pos.y == 6.0f);
        REQUIRE(health.value == 100);
    }
}

TEST_CASE("ECS Integration Test", "[ecs][integration]") {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Velocity>();
    Registry::instance().register_type<Health>();
    Registry::instance().register_type<Name>();
    Registry::instance().register_type<GameConfig>();
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(GameConfig {});
    world.add_resource(CommandsQueue {});

    SECTION("Complex entity management scenario") {
        // Create some initial entities
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

        // Movement system
        world.run_system_once([](Query<Position, Velocity> query) {
            for (auto [pos, vel] : query) {
                pos.x += vel.dx;
                pos.y += vel.dy;
            }
        });

        // Spawn system using commands
        world.run_system_once([](Commands commands, Res<GameConfig> config) {
            // Spawn a new enemy
            commands.spawn()
                .add(Position(20.0f, 20.0f))
                .add(Velocity(-0.5f, -0.5f))
                .add(Health(25))
                .add(Name("Enemy2"));
        });

        world.get_resource<CommandsQueue>().execute(world);

        // Verify movement system worked
        Position& player_pos = world.get_component<Position>(player);
        REQUIRE(player_pos.x == 0.0f); // Player velocity was 0
        REQUIRE(player_pos.y == 0.0f);

        Position& enemy1_pos = world.get_component<Position>(enemy1);
        REQUIRE(enemy1_pos.x == 9.0f); // Moved by velocity (-1, 0)
        REQUIRE(enemy1_pos.y == 10.0f);

        // Count entities with Health component
        int entity_count = 0;
        world.run_system_once([&entity_count](Query<Entity, Health> query) {
            for (auto [entity, health] : query) {
                (void)entity;
                (void)health; // Use variables to avoid warnings
                entity_count++;
            }
        });

        REQUIRE(entity_count == 3); // Player, Enemy1, and spawned Enemy2
    }
}

TEST_CASE("ECS Event System", "[ecs][event]") {
    // Register event types
    Registry::instance().register_type<PlayerMoved>();
    Registry::instance().register_type<GameEvent>();
    Registry::instance().register_type<EventsMap>();
    Registry::instance().register_type<Position>();

    World world;
    world.add_resource(EventsMap {});

    SECTION("Basic event sending and reading") {
        Entity player = world.entity();
        Position from(0.0f, 0.0f);
        Position to(5.0f, 10.0f);

        bool event_sent = false;
        bool event_received = false;
        PlayerMoved received_event;

        // System that sends events
        world.run_system_once([&](EventWriter<PlayerMoved> writer) {
            writer.send(PlayerMoved(player, from, to));
            event_sent = true;
        });

        // System that reads events
        world.run_system_once([&](EventReader<PlayerMoved> reader) {
            auto event = reader.next();
            if (event.has_value()) {
                event_received = true;
                received_event = event.value();
            }
        });

        REQUIRE(event_sent);
        REQUIRE(event_received);
        REQUIRE(received_event == PlayerMoved(player, from, to));
    }

    SECTION("Multiple events") {
        std::vector<GameEvent> sent_events =
            {GameEvent("Event 1"), GameEvent("Event 2"), GameEvent("Event 3")};
        std::vector<GameEvent> received_events;

        // Send events
        world.run_system_once([&](EventWriter<GameEvent> writer) {
            for (const auto& event : sent_events) {
                writer.send(GameEvent(event.message));
            }
        });

        // Read events
        world.run_system_once([&](EventReader<GameEvent> reader) {
            while (auto event = reader.next()) {
                received_events.push_back(event.value());
            }
        });

        REQUIRE(received_events.size() == 3);
        REQUIRE(received_events == sent_events);
    }

    SECTION("Event reader isolation") {
        // Send some events
        world.run_system_once([](EventWriter<GameEvent> writer) {
            writer.send(GameEvent("Test Event"));
        });

        int reader1_count = 0;
        int reader2_count = 0;

        // Two separate readers should both see the same events
        world.run_system_once([&](EventReader<GameEvent> reader) {
            while (reader.next().has_value()) {
                reader1_count++;
            }
        });

        world.run_system_once([&](EventReader<GameEvent> reader) {
            while (reader.next().has_value()) {
                reader2_count++;
            }
        });

        REQUIRE(reader1_count == 1);
        REQUIRE(reader2_count == 1);
    }
}
