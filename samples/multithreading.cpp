#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_config.hpp"
#include "ecs/world.hpp"

#include <chrono>
#include <cstddef>
#include <format>
#include <functional>
#include <mutex>
#include <print>
#include <string_view>
#include <thread>

using namespace fei;
using namespace std::chrono_literals;

namespace {

struct Position {
    float x {0.0f};
    float y {0.0f};
};

struct Velocity {
    float dx {0.0f};
    float dy {0.0f};
};

constexpr ScheduleId DemoSchedule = 1000;

std::mutex g_log_mutex;
std::chrono::steady_clock::time_point g_run_start;

void log(std::string_view message) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_run_start)
            .count();
    auto thread_id = std::hash<std::thread::id> {}(std::this_thread::get_id());

    std::scoped_lock lock(g_log_mutex);
    std::println("{:>4} ms | thread {:016x} | {}", elapsed, thread_id, message);
}

void read_positions(Query<const Position> query) {
    log("read_positions start");

    std::size_t count = 0;
    for (auto [position] : query) {
        if (position.x >= 0.0f) {
            ++count;
        }
    }

    std::this_thread::sleep_for(300ms);
    log(std::format("read_positions done, {} positions", count));
}

void read_velocities(Query<const Velocity> query) {
    log("read_velocities start");

    std::size_t count = 0;
    for (auto [velocity] : query) {
        if (velocity.dx >= 0.0f) {
            ++count;
        }
    }

    std::this_thread::sleep_for(300ms);
    log(std::format("read_velocities done, {} velocities", count));
}

void integrate_positions(Query<Position, const Velocity> query) {
    log("integrate_positions start");

    for (auto [position, velocity] : query) {
        position.x += velocity.dx;
        position.y += velocity.dy;
    }

    std::this_thread::sleep_for(80ms);
    log("integrate_positions done");
}

World make_world(std::size_t worker_threads) {
    World world;
    world.set_worker_threads(worker_threads);
    world.add_resource(CommandsQueue {});

    for (int i = 0; i < 100; ++i) {
        auto entity = world.entity();
        world.add_component(entity, Position {static_cast<float>(i), 0.0f});
        world.add_component(entity, Velocity {1.0f, 0.25f});
    }

    auto read_positions_config = SystemConfig(read_positions);
    auto read_velocities_config = SystemConfig(read_velocities);
    auto integrate_positions_config = SystemConfig(integrate_positions);
    integrate_positions_config.after(read_positions_config);
    integrate_positions_config.after(read_velocities_config);
    world.add_systems(
        DemoSchedule,
        std::move(read_positions_config),
        std::move(read_velocities_config),
        std::move(integrate_positions_config)
    );
    world.sort_systems();

    return world;
}

void run_demo(std::size_t worker_threads) {
    auto world = make_world(worker_threads);

    std::println("\n=== {} worker thread(s) ===", world.worker_threads());
    g_run_start = std::chrono::steady_clock::now();
    world.run_schedule(DemoSchedule);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - g_run_start
    )
                       .count();
    std::println("finished in {} ms", elapsed);
}

} // namespace

int main() {
    run_demo(1);
    run_demo(2);
    return 0;
}
