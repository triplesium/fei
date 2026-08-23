#include "play_trace_store.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <nlohmann/json.hpp>

using namespace ets;
using namespace ets::agentd;

TEST_CASE(
    "Play trace store assigns sequences and reads incrementally",
    "[agentd][trace]"
) {
    PlayTraceStore store;
    auto first = store.append({{"type", "span.started"}, {"name", "step"}});
    auto second = store.append({{"type", "span.completed"}, {"name", "step"}});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == 1);
    CHECK(*second == 2);

    const auto batch = store.read_after(1, 20);
    CHECK(batch.oldest == 1);
    CHECK(batch.latest == 2);
    CHECK_FALSE(batch.gap);
    REQUIRE(batch.events.size() == 1);
    CHECK(batch.events.front().at("sequence") == 2);
    CHECK(batch.events.front().contains("timestamp_ms"));
}

TEST_CASE(
    "Play trace store reports gaps after bounded eviction",
    "[agentd][trace]"
) {
    PlayTraceStore store(
        PlayTraceLimits {
            .maximum_events = 2,
            .maximum_bytes = 4096,
            .maximum_event_bytes = 1024,
        }
    );
    REQUIRE(store.append({{"type", "event"}, {"name", "one"}}));
    REQUIRE(store.append({{"type", "event"}, {"name", "two"}}));
    REQUIRE(store.append({{"type", "event"}, {"name", "three"}}));
    REQUIRE(store.append({{"type", "event"}, {"name", "four"}}));

    const auto batch = store.read_after(0, 20);
    CHECK(batch.oldest == 3);
    CHECK(batch.latest == 4);
    CHECK_FALSE(batch.gap);
    REQUIRE(batch.events.size() == 2);

    const auto missed = store.read_after(1, 20);
    CHECK(missed.gap);
}

TEST_CASE(
    "Play trace store truncates oversized event payloads",
    "[agentd][trace]"
) {
    PlayTraceStore store(
        PlayTraceLimits {
            .maximum_events = 10,
            .maximum_bytes = 1024,
            .maximum_event_bytes = 128,
        }
    );
    REQUIRE(store.append(
        {{"type", "span.completed"},
         {"name", "observe"},
         {"data", std::string(512, 'x')}}
    ));

    const auto batch = store.read_after(0, 10);
    REQUIRE(batch.events.size() == 1);
    CHECK(batch.events.front().at("data").at("truncated") == true);
    CHECK(batch.events.front().at("data").at("original_bytes") > 128);
}

TEST_CASE("Play trace store wakes long poll readers", "[agentd][trace]") {
    PlayTraceStore store;
    auto reader = std::async(std::launch::async, [&store]() {
        return store.wait_after(0, 10, std::chrono::seconds(1));
    });
    REQUIRE(store.append({{"type", "play_log"}, {"name", "log"}}));

    const auto batch = reader.get();
    REQUIRE(batch.events.size() == 1);
    CHECK(batch.latest == 1);
}
