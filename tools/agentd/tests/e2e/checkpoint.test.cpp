#include "base/result.hpp"
#include "process.hpp"
#include "project/project.hpp"
#include "project_descriptor.hpp"
#include "server.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <httplib.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>

using namespace fei;
using namespace fei::agentd;

namespace {

using Json = nlohmann::json;
using JsonResult = Result<Json, std::string>;

JsonResult
inspection_payload(httplib::Result response, std::string_view operation) {
    if (!response) {
        return failure(std::string(operation) + " could not reach agentd");
    }
    try {
        auto document = Json::parse(response->body);
        if (response->status < 200 || response->status >= 300 ||
            !document.is_object() || !document.value("ok", false)) {
            return failure(
                document.value("message", std::string(operation) + " failed")
            );
        }
        if (!document.contains("payload")) {
            return failure(std::string(operation) + " returned no payload");
        }
        return std::move(document.at("payload"));
    } catch (const std::exception& error) {
        return failure(
            std::string(operation) + " returned invalid JSON: " + error.what()
        );
    }
}

} // namespace

TEST_CASE(
    "Agent control script replays and branches a real supervised ECS world",
    "[agentd][play-run][checkpoint][e2e]"
) {
    auto project = Project::load(FEI_CHECKPOINT_PROJECT_PATH);
    REQUIRE(project);

    SupervisorState state(
        "checkpoint-play-e2e-session",
        describe_project(*project)
    );
    const auto first_port = static_cast<uint16>(
        41000 +
        std::chrono::steady_clock::now().time_since_epoch().count() % 1000
    );
    std::unique_ptr<AgentServer> server;
    uint16 port = first_port;
    for (uint16 attempt = 0; attempt < 100; ++attempt) {
        port = static_cast<uint16>(first_port + attempt);
        auto candidate = std::make_unique<AgentServer>(state, port);
        if (candidate->start()) {
            server = std::move(candidate);
            break;
        }
    }
    REQUIRE(server != nullptr);

    state.mark_process_started(1);
    RuntimeProcess runtime;
    REQUIRE(runtime.start(
        ProcessLaunch {
            .arguments =
                {
                    FEI_SNAPSHOT_RUNTIME_FIXTURE_PATH,
                    FEI_CHECKPOINT_PROJECT_PATH,
                },
            .environment = {
                {"FEI_AGENTD_PORT", std::to_string(port)},
                {"FEI_RUNTIME_SESSION", state.session()},
            },
        }
    ));

    const auto connection_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool connected = false;
    while (std::chrono::steady_clock::now() < connection_deadline) {
        const auto status = Json::parse(state.status_json());
        if (status.at("runtime").at("connection") == "connected") {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(connected);

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(0, 500000);
    client.set_read_timeout(35, 0);
    client.set_write_timeout(1, 0);

    constexpr std::string_view source = R"(
local function same(left, right)
    return left.turn == right.turn
        and left.score == right.score
        and left.simulation_tick == right.simulation_tick
        and left.last_roll == right.last_roll
        and left.player.x == right.player.x
        and left.player.health == right.player.health
        and left.enemy.alive == right.enemy.alive
        and left.enemy.health == right.enemy.health
end

local saved = play.checkpoint("before-combat", true)
local first = play.step("game.combat", { kind = "attack" }).observation
local second = play.step("game.combat", { kind = "attack" }).observation

play.restore("before-combat")
local replay_first = play.step("game.combat", { kind = "attack" }).observation
local replay_second = play.step("game.combat", { kind = "attack" }).observation
assert(same(first, replay_first), "first replay diverged")
assert(same(second, replay_second), "second replay diverged")

play.restore("before-combat")
local alternate = play.step("game.combat", { kind = "wait" }).observation
assert(alternate.player.health ~= first.player.health, "branch did not diverge")

return {
    checkpoint = saved.name,
    replay_matches = true,
    branch_changed = true,
    first_roll = first.last_roll,
    final_score = second.score,
    alternate_health = alternate.player.health,
}
)";

    RuntimeProcess control;
    REQUIRE(control.start(
        ProcessLaunch {
            .arguments = {
                FEI_CTL_PATH,
                "--port",
                std::to_string(port),
                "play-run",
                "--eval",
                std::string(source),
            },
        }
    ));
    const auto control_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(40);
    Optional<int> exit_code;
    while (std::chrono::steady_clock::now() < control_deadline) {
        exit_code = control.poll();
        if (exit_code) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(exit_code);
    CHECK(*exit_code == 0);

    auto final_state = inspection_payload(
        client.Post(
            "/api/v1/play/observe",
            Json {{"interface", "game.combat"}}.dump(),
            "application/json"
        ),
        "final play.observe"
    );
    REQUIRE(final_state);
    const auto& observation = final_state->at("observation");
    CHECK(observation.at("turn") == 1);
    CHECK(observation.at("simulation_tick") == 1);
    CHECK(observation.at("score") == 0);
    CHECK(observation.at("player").at("health") == 10);
    CHECK(observation.at("enemy").at("alive") == true);
    CHECK(observation.at("enemy").at("health") == 7);
}
