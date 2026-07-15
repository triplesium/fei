#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::ecs_test;

TEST_CASE("ECS events can be sent, read, and aged", "[ecs][event]") {
    Registry::instance().register_type<PlayerMoved>();
    Registry::instance().register_type<GameEvent>();
    Registry::instance().register_type<Events<GameEvent>>();
    Registry::instance().register_type<Events<PlayerMoved>>();
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<EventStats>();

    World world;
    world.add_resource(Events<GameEvent> {});
    world.add_resource(Events<PlayerMoved> {});

    SECTION("Basic event sending and reading") {
        Entity player = world.entity();
        Position from(0.0f, 0.0f);
        Position to(5.0f, 10.0f);

        bool event_sent = false;
        bool event_received = false;
        PlayerMoved received_event;

        world.run_system_once([&](EventWriter<PlayerMoved> writer) {
            writer.send(PlayerMoved(player, from, to));
            event_sent = true;
        });

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

        world.run_system_once([&](EventWriter<GameEvent> writer) {
            for (const auto& event : sent_events) {
                writer.send(GameEvent(event.message));
            }
        });

        world.run_system_once([&](EventReader<GameEvent> reader) {
            while (auto event = reader.next()) {
                received_events.push_back(event.value());
            }
        });

        REQUIRE(received_events == sent_events);
    }

    SECTION("Event reader isolation") {
        world.run_system_once([](EventWriter<GameEvent> writer) {
            writer.send(GameEvent("Test Event"));
        });

        int reader1_count = 0;
        int reader2_count = 0;

        world.run_system_once([&](EventReader<GameEvent> reader) {
            while (reader.next().has_value()) {
                ++reader1_count;
            }
        });

        world.run_system_once([&](EventReader<GameEvent> reader) {
            while (reader.next().has_value()) {
                ++reader2_count;
            }
        });

        REQUIRE(reader1_count == 1);
        REQUIRE(reader2_count == 1);
    }

    SECTION(
        "Event reader skips expired cursor and continues with live events"
    ) {
        auto& events = world.resource<Events<GameEvent>>();
        std::size_t cursor = 0;
        EventReader<GameEvent> reader(events, cursor);

        events.send(GameEvent("stale"));
        events.update();
        events.update();
        events.send(GameEvent("live"));

        auto event = reader.next();
        REQUIRE(event.has_value());
        REQUIRE(event->message == "live");
        REQUIRE_FALSE(reader.next().has_value());
    }

    SECTION("Scheduled reader initializes cursor from current event window") {
        world.add_resource(CommandsQueue {});
        world.add_resource(EventStats {});

        auto& events = world.resource<Events<GameEvent>>();
        events.send(GameEvent("stale"));
        events.update();
        events.update();

        world.add_systems(
            TestSchedule,
            chain(scheduled_send_event, scheduled_read_events)
        );
        world.sort_systems();

        world.run_schedule(TestSchedule);
        REQUIRE(
            world.resource<EventStats>().messages ==
            std::vector<std::string> {"tick"}
        );
    }

    SECTION("Scheduled reader keeps cursor while events age across updates") {
        world.add_resource(CommandsQueue {});
        world.add_resource(EventStats {});

        world.add_systems(
            TestSchedule,
            chain(
                scheduled_send_event,
                scheduled_read_events,
                scheduled_update_events
            )
        );
        world.sort_systems();

        world.run_schedule(TestSchedule);
        REQUIRE(
            world.resource<EventStats>().messages ==
            std::vector<std::string> {"tick"}
        );
        REQUIRE(world.resource<Events<GameEvent>>().size() == 1);

        world.run_schedule(TestSchedule);
        REQUIRE(
            world.resource<EventStats>().messages ==
            std::vector<std::string> {"tick", "tick"}
        );
        REQUIRE(world.resource<Events<GameEvent>>().size() == 1);

        world.resource<Events<GameEvent>>().update();
        REQUIRE(world.resource<Events<GameEvent>>().size() == 0);
    }
}
