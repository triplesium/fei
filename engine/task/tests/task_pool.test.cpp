#include "task/task_pool.hpp"

#include "app/app.hpp"
#include "task/plugin.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace fei;
using namespace std::chrono_literals;

namespace {

template<typename Done>
void drain_until(TaskPool& pool, Done done) {
    for (int i = 0; i < 1000 && !done(); ++i) {
        pool.drain_completions();
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(done());
}

} // namespace

TEST_CASE("TaskPool drains task completions on the calling thread", "[task]") {
    TaskPool pool(1);
    std::thread::id completion_thread;
    int completed_value = 0;

    pool.submit(
        []() {
            return 42;
        },
        [&](TaskResult<int> result) {
            completion_thread = std::this_thread::get_id();
            completed_value = result.value();
        }
    );

    drain_until(pool, [&]() {
        return completed_value == 42;
    });

    REQUIRE(completion_thread == std::this_thread::get_id());
}

TEST_CASE("TaskPool supports move-only task results", "[task]") {
    TaskPool pool(1);
    std::unique_ptr<int> completed_value;

    pool.submit(
        []() {
            return std::make_unique<int>(7);
        },
        [&](TaskResult<std::unique_ptr<int>> result) {
            completed_value = std::move(result).value();
        }
    );

    drain_until(pool, [&]() {
        return completed_value != nullptr;
    });

    REQUIRE(*completed_value == 7);
}

TEST_CASE("TaskPool forwards task failures to completions", "[task]") {
    TaskPool pool(1);
    std::string message;

    pool.submit(
        []() -> int {
            throw std::runtime_error("task failed");
        },
        [&](TaskResult<int> result) {
            REQUIRE_FALSE(result.has_value());
            try {
                result.rethrow_if_error();
            } catch (const std::runtime_error& error) {
                message = error.what();
            }
        }
    );

    drain_until(pool, [&]() {
        return message == "task failed";
    });
}

TEST_CASE("TaskPlugin drains completions during PostUpdate", "[task][plugin]") {
    App app;
    app.add_plugin<TaskPlugin>();
    app.world().sort_systems();

    bool completed = false;
    app.resource<Tasks>().general().submit(
        []() {
            return;
        },
        [&](TaskResult<void> result) {
            result.value();
            completed = true;
        }
    );

    for (int i = 0; i < 1000 && !completed; ++i) {
        app.run_schedule(PostUpdate);
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(completed);
}
