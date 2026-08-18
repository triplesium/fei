#include "play_runner.hpp"

#include "base/result.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace fei;
using namespace fei::agentd;

namespace {

PlayControlBindings test_bindings(std::size_t& step_count) {
    return PlayControlBindings {
        .interfaces = []() -> Result<nlohmann::json, std::string> {
            return nlohmann::json {
                {"interfaces",
                 nlohmann::json::array({
                     {
                         {"id", "game.main"},
                         {"decision_ticks", 5},
                     },
                 })},
            };
        },
        .step = [&step_count](
                    std::string_view interface_id,
                    const nlohmann::json& action,
                    std::optional<uint32> ticks
                ) -> Result<nlohmann::json, std::string> {
            ++step_count;
            return nlohmann::json {
                {"interface", interface_id},
                {"ticks", ticks.value_or(5)},
                {"frame", step_count * ticks.value_or(5)},
                {"stopped", false},
                {"action", action},
                {"observation", {{"score", step_count}}},
            };
        },
        .observe = [&step_count](std::string_view interface_id)
            -> Result<nlohmann::json, std::string> {
            return nlohmann::json {
                {"interface", interface_id},
                {"frame", step_count * 5},
                {"observation", {{"score", step_count}}},
            };
        },
        .capture = [](const std::optional<std::string>& output)
            -> Result<nlohmann::json, std::string> {
            nlohmann::json result {
                {"artifact", "/api/v1/artifacts/frame"},
                {"content_type", "image/png"},
            };
            if (output) {
                result["output"] = *output;
            }
            return result;
        },
    };
}

} // namespace

TEST_CASE(
    "Luau play runner executes dynamic control logic and records calls",
    "[agentd][play-run]"
) {
    std::size_t step_count = 0;
    const auto bindings = test_bindings(step_count);
    constexpr std::string_view source = R"(
local interfaces = play.interfaces()
local before = play.observe("game.main")
local first = play.step("game.main", { move = "right" }, 3)
if before.score == 0 then
    play.step("game.main", { move = "up" })
end
local capture = play.capture("frame.png")
print("done", first.ticks)
return {
    interface_count = #interfaces,
    score = play.observe("game.main").score,
    artifact = capture.artifact,
}
)";

    const auto report = run_luau_play_script(source, bindings);

    INFO(report.dump(2));
    REQUIRE(report.at("ok").get<bool>());
    CHECK(report.at("result").at("interface_count") == 1);
    CHECK(report.at("result").at("score") == 2);
    CHECK(report.at("result").at("artifact") == "/api/v1/artifacts/frame");
    CHECK(report.at("calls").size() == 6);
    CHECK(report.at("budget").at("calls") == 6);
    CHECK(report.at("budget").at("ticks") == 8);
    CHECK(report.at("logs") == nlohmann::json::array({{"done", 3}}));
}

TEST_CASE(
    "Luau play runner exposes only its bounded control environment",
    "[agentd][play-run][sandbox]"
) {
    std::size_t step_count = 0;
    const auto report = run_luau_play_script(
        R"(
assert(os == nil)
assert(io == nil)
assert(debug == nil)
assert(require == nil)
assert(loadstring == nil)
return { math = math.floor(2.8), has_play = play ~= nil }
)",
        test_bindings(step_count)
    );

    INFO(report.dump(2));
    REQUIRE(report.at("ok").get<bool>());
    CHECK(
        report.at("result") == nlohmann::json({
                                   {"math", 2},
                                   {"has_play", true},
                               })
    );
}

TEST_CASE(
    "Luau play runner traces control adapter failures",
    "[agentd][play-run][error]"
) {
    std::size_t step_count = 0;
    auto bindings = test_bindings(step_count);
    bindings.observe =
        [](std::string_view) -> Result<nlohmann::json, std::string> {
        throw std::runtime_error("transport failed");
    };

    const auto report =
        run_luau_play_script(R"(return play.observe("game.main"))", bindings);

    INFO(report.dump(2));
    CHECK_FALSE(report.at("ok").get<bool>());
    REQUIRE(report.at("calls").size() == 1);
    CHECK_FALSE(report.at("calls").at(0).at("ok").get<bool>());
    CHECK(report.at("calls").at(0).at("error") == "transport failed");
}

TEST_CASE(
    "Luau play runner stops scripts that exceed execution budgets",
    "[agentd][play-run][budget]"
) {
    std::size_t step_count = 0;
    auto limits = PlayRunLimits {};
    limits.maximum_ticks = 10;

    auto report = run_luau_play_script(
        R"(return play.step("game.main", { move = "right" }, 11))",
        test_bindings(step_count),
        limits
    );
    INFO(report.dump(2));
    CHECK_FALSE(report.at("ok").get<bool>());
    CHECK(
        report.at("error").get<std::string>().find("tick limit") !=
        std::string::npos
    );
    CHECK(report.at("calls").empty());
    CHECK(step_count == 0);

    limits.maximum_interrupts = 100;
    limits.maximum_duration = std::chrono::seconds(1);
    report = run_luau_play_script(
        "while true do end",
        test_bindings(step_count),
        limits
    );
    INFO(report.dump(2));
    CHECK_FALSE(report.at("ok").get<bool>());
    CHECK(
        report.at("error").get<std::string>().find("budget exceeded") !=
        std::string::npos
    );
}

TEST_CASE(
    "Luau play runner reports source and compilation errors",
    "[agentd][play-run][error]"
) {
    std::size_t step_count = 0;
    auto limits = PlayRunLimits {};
    limits.maximum_source_bytes = 4;
    auto report =
        run_luau_play_script("return true", test_bindings(step_count), limits);
    CHECK_FALSE(report.at("ok").get<bool>());
    CHECK(
        report.at("error").get<std::string>().find("size limit") !=
        std::string::npos
    );

    limits.maximum_source_bytes = 1024;
    report = run_luau_play_script(
        "local = broken",
        test_bindings(step_count),
        limits
    );
    INFO(report.dump(2));
    CHECK_FALSE(report.at("ok").get<bool>());
    CHECK(
        report.at("error").get<std::string>().find("play-run") !=
        std::string::npos
    );
}
