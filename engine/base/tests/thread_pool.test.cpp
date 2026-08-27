#include "base/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

using namespace ets;

#ifndef __EMSCRIPTEN__
TEST_CASE("ThreadPool runs submitted tasks", "[base][thread_pool]") {
    ThreadPool pool(2);

    auto first = pool.submit([]() {
        return 2;
    });
    auto second = pool.submit([]() {
        return 5;
    });

    REQUIRE(pool.thread_count() == 2);
    REQUIRE(first.get() == 2);
    REQUIRE(second.get() == 5);
}

TEST_CASE("ThreadPool propagates task exceptions", "[base][thread_pool]") {
    ThreadPool pool(1);

    auto task = pool.submit([]() -> int {
        throw std::runtime_error("failed");
    });

    REQUIRE_THROWS_AS(task.get(), std::runtime_error);
}

TEST_CASE(
    "ThreadPool exposes stable pool-local worker indices",
    "[base][thread_pool]"
) {
    ThreadPool pool(2);
    ThreadPool other_pool(0);
    std::mutex mutex;
    std::condition_variable ready;
    int entered = 0;

    auto observe_worker = [&]() -> Optional<std::size_t> {
        auto first = pool.current_worker_index();
        if (!first || other_pool.current_worker_index()) {
            return nullopt;
        }
        {
            std::unique_lock lock(mutex);
            ++entered;
            ready.notify_all();
            if (!ready.wait_for(lock, std::chrono::seconds {1}, [&]() {
                    return entered == 2;
                })) {
                return nullopt;
            }
        }
        const auto second = pool.current_worker_index();
        if (!second || *second != *first) {
            return nullopt;
        }
        return first;
    };

    auto first = pool.submit(observe_worker);
    auto second = pool.submit(observe_worker);
    const auto first_index = first.get();
    const auto second_index = second.get();

    REQUIRE(first_index);
    REQUIRE(second_index);
    REQUIRE(*first_index != *second_index);
    REQUIRE(*first_index < pool.thread_count());
    REQUIRE(*second_index < pool.thread_count());
    REQUIRE_FALSE(pool.current_worker_index());
    REQUIRE_FALSE(other_pool.current_worker_index());
}
#endif

TEST_CASE(
    "ThreadPool runs tasks inline without workers",
    "[base][thread_pool]"
) {
    ThreadPool pool(0);
    bool ran = false;

    auto task = pool.submit([&ran]() {
        ran = true;
        return 7;
    });

    REQUIRE(pool.thread_count() == 0);
    REQUIRE(ran);
    REQUIRE(task.get() == 7);
    REQUIRE_FALSE(pool.current_worker_index());
}
