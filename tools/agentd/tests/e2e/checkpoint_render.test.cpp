#include "base/optional.hpp"
#include "base/result.hpp"
#include "process.hpp"
#include "project/project.hpp"
#include "project_descriptor.hpp"
#include "server.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

class RenderedCheckpointFiles {
  private:
    std::filesystem::path m_directory;

  public:
    RenderedCheckpointFiles() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory =
            std::filesystem::temp_directory_path() /
            ("fei-agentd-rendered-checkpoint-e2e-" + std::to_string(suffix));
        std::filesystem::create_directories(m_directory);
    }

    ~RenderedCheckpointFiles() {
        std::error_code error;
        std::filesystem::remove_all(m_directory, error);
    }

    [[nodiscard]] std::filesystem::path archive() const {
        return m_directory / "gate-cleared.fei-snapshot.json";
    }

    [[nodiscard]] std::filesystem::path before_frame() const {
        return m_directory / "gate-cleared-before.png";
    }

    [[nodiscard]] std::filesystem::path after_frame() const {
        return m_directory / "gate-cleared-after.png";
    }
};

std::vector<char> read_binary(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
}

bool wait_for_connection(SupervisorState& state) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = Json::parse(state.status_json());
        if (status.at("runtime").at("connection") == "connected") {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

} // namespace

TEST_CASE(
    "Rendered Luau checkpoint survives a real Runtime Host restart",
    "[agentd][checkpoint][runtime-host][physics2d][asset][ui][luau][e2e]"
) {
    auto project = Project::load(FEI_CHECKPOINT_RENDER_PROJECT_PATH);
    REQUIRE(project);

    SupervisorState state(
        "rendered-checkpoint-e2e-session",
        describe_project(*project)
    );
    const auto first_port = static_cast<uint16>(
        43000 +
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

    auto launch_runtime = [&](RuntimeProcess& runtime) {
        return runtime.start(
            ProcessLaunch {
                .arguments =
                    {
                        FEI_RUNTIME_HOST_PATH,
                        FEI_CHECKPOINT_RENDER_PROJECT_PATH,
                    },
                .environment = {
                    {"FEI_AGENTD_PORT", std::to_string(port)},
                    {"FEI_RUNTIME_SESSION", state.session()},
                },
            }
        );
    };

    state.mark_process_started(1);
    RuntimeProcess runtime;
    REQUIRE(launch_runtime(runtime));
    REQUIRE(wait_for_connection(state));

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(0, 500000);
    client.set_read_timeout(40, 0);
    client.set_write_timeout(1, 0);

    auto step = [&](std::string_view kind, int power, uint32 ticks) {
        return inspection_payload(
            client.Post(
                "/api/v1/play/step",
                Json {
                    {"interface", "checkpoint-arena.main"},
                    {"action", {{"kind", kind}, {"power", power}}},
                    {"ticks", ticks},
                }
                    .dump(),
                "application/json"
            ),
            "play.step"
        );
    };
    auto observe = [&]() {
        return inspection_payload(
            client.Post(
                "/api/v1/play/observe",
                Json {{"interface", "checkpoint-arena.main"}}.dump(),
                "application/json"
            ),
            "play.observe"
        );
    };
    auto run_ctl = [&](std::vector<std::string> arguments) {
        std::vector<std::string> command {
            FEI_CTL_PATH,
            "--port",
            std::to_string(port),
        };
        command.insert(
            command.end(),
            std::make_move_iterator(arguments.begin()),
            std::make_move_iterator(arguments.end())
        );
        RuntimeProcess process;
        if (!process.start(ProcessLaunch {.arguments = std::move(command)})) {
            return false;
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(45);
        while (std::chrono::steady_clock::now() < deadline) {
            if (const auto code = process.poll()) {
                return *code == 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        process.terminate();
        return false;
    };

    REQUIRE(step("advance", 0, 80));
    REQUIRE(step("strike", 3, 1));
    auto gate_cleared = step("strike", 3, 1);
    REQUIRE(gate_cleared);
    const auto checkpoint_observation = gate_cleared->at("observation");
    CHECK(checkpoint_observation.at("score") == 10);
    CHECK(checkpoint_observation.at("actions") == 3);
    CHECK(checkpoint_observation.at("enemy").at("alive") == false);
    CHECK(checkpoint_observation.at("physics").at("x").get<float>() > -1.0f);
    CHECK(checkpoint_observation.at("physics").at("y").get<float>() < 1.65f);

    RenderedCheckpointFiles files;
    REQUIRE(run_ctl({"checkpoint-create", "gate-cleared", "--strict"}));
    REQUIRE(run_ctl({
        "play-capture",
        "--output",
        files.before_frame().string(),
    }));

    auto branch = step("retreat", 0, 12);
    REQUIRE(branch);
    const auto branch_observation = branch->at("observation");
    CHECK(
        branch_observation.at("player").at("x").get<float>() <
        checkpoint_observation.at("player").at("x").get<float>()
    );
    CHECK(
        branch_observation.at("physics").at("y").get<float>() <
        checkpoint_observation.at("physics").at("y").get<float>()
    );

    REQUIRE(run_ctl({
        "checkpoint-export",
        "gate-cleared",
        "--output",
        files.archive().string(),
    }));
    REQUIRE(std::filesystem::is_regular_file(files.archive()));
    REQUIRE(std::filesystem::file_size(files.archive()) > 0);
    REQUIRE(std::filesystem::is_regular_file(files.before_frame()));
    const auto before_frame = read_binary(files.before_frame());
    REQUIRE(before_frame.size() > 8);
    CHECK(std::string_view(before_frame.data(), 8) == "\x89PNG\r\n\x1a\n");

    runtime.terminate();
    state.mark_process_exited(1);
    state.mark_process_started(2);
    REQUIRE(launch_runtime(runtime));
    REQUIRE(wait_for_connection(state));

    REQUIRE(run_ctl({
        "checkpoint-import",
        "gate-cleared",
        "--input",
        files.archive().string(),
    }));
    REQUIRE(run_ctl({"checkpoint-restore", "gate-cleared"}));

    auto restored = observe();
    REQUIRE(restored);
    CHECK(restored->at("observation") == checkpoint_observation);

    REQUIRE(run_ctl({
        "play-capture",
        "--output",
        files.after_frame().string(),
    }));
    const auto after_frame = read_binary(files.after_frame());
    CHECK(after_frame == before_frame);

    auto replayed_branch = step("retreat", 0, 12);
    REQUIRE(replayed_branch);
    CHECK(replayed_branch->at("observation") == branch_observation);
}
