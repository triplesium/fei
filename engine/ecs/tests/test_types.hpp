#pragma once

#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_set.hpp"
#include "refl/registry.hpp"

#include <string>
#include <vector>

namespace fei::ecs_test {

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
    explicit Health(int value) : value(value) {}

    bool operator==(const Health& other) const { return value == other.value; }
};

struct Name {
    std::string value;

    Name() = default;
    explicit Name(const std::string& value) : value(value) {}

    bool operator==(const Name& other) const { return value == other.value; }
};

struct GameConfig {
    int max_entities = 1000;
    float dt = 0.016f;
};

struct EventQueue {
    std::vector<std::string> events;

    void push(const std::string& event) { events.push_back(event); }
};

struct PlayerMoved {
    Entity player {};
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
    explicit GameEvent(const std::string& message) : message(message) {}

    bool operator==(const GameEvent& other) const {
        return message == other.message;
    }
};

constexpr ScheduleId TestSchedule = 123;

struct ScheduleTrace {
    std::vector<std::string> entries;
};

struct EventStats {
    std::vector<std::string> messages;
};

struct ScheduleFirstSet : SystemSet<ScheduleFirstSet> {};
struct ScheduleSecondSet : SystemSet<ScheduleSecondSet> {};

inline void register_components() {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<Velocity>();
    Registry::instance().register_type<Health>();
    Registry::instance().register_type<Name>();
}

inline void scheduled_spawn_position(Commands commands) {
    commands.spawn().add(Position(30.0f, 40.0f));
}

inline void scheduled_count_positions(
    Query<Entity, Position> query,
    ResRW<ScheduleTrace> trace
) {
    trace->entries.push_back("count:" + std::to_string(query.size()));
}

inline void scheduled_first(ResRW<ScheduleTrace> trace) {
    trace->entries.emplace_back("first");
}

inline void scheduled_second(ResRW<ScheduleTrace> trace) {
    trace->entries.emplace_back("second");
}

inline void scheduled_send_event(EventWriter<GameEvent> writer) {
    writer.send(GameEvent("tick"));
}

inline void
scheduled_read_events(EventReader<GameEvent> reader, ResRW<EventStats> stats) {
    while (auto event = reader.next()) {
        stats->messages.push_back(event->message);
    }
}

inline void scheduled_update_events(ResRW<Events<GameEvent>> events) {
    events->update();
}

} // namespace fei::ecs_test
