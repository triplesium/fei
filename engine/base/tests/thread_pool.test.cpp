#include "base/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

using namespace fei;

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
