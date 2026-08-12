#include "base/log.hpp"
#include "base/types.hpp"
#include "process.hpp"
#include "project/project.hpp"
#include "project_descriptor.hpp"
#include "server.hpp"
#include "state.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace fei;
using namespace fei::agentd;

std::atomic<bool> running {true};

void handle_signal(int) {
    running.store(false, std::memory_order_relaxed);
}

struct Options {
    uint16 port {8091};
    bool show_help {false};
    bool external_runtime {false};
    std::filesystem::path project_file;
    std::optional<std::string> runtime_executable;
    std::vector<std::string> runtime_command;
};

void print_help() {
    std::cout << "usage: fei-agentd --project PROJECT [--port PORT]\n"
              << "       fei-agentd --project PROJECT --runtime EXECUTABLE\n"
              << "       fei-agentd --project PROJECT --external-runtime\n";
}

Result<uint16, std::string> parse_port(std::string_view value) {
    uint16 port {};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc {} || end != value.data() + value.size() ||
        port == 0) {
        return failure(
            std::string("Invalid agentd port: ") + std::string(value)
        );
    }
    return port;
}

Result<Options, std::string> parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            continue;
        }
        if (argument == "--port") {
            if (++index >= argc) {
                return failure(std::string("--port requires a value"));
            }
            auto port = parse_port(argv[index]);
            if (!port) {
                return failure(std::move(port.error()));
            }
            options.port = *port;
            continue;
        }
        if (argument == "--project") {
            if (++index >= argc) {
                return failure(std::string("--project requires a value"));
            }
            options.project_file = argv[index];
            continue;
        }
        if (argument == "--runtime") {
            if (++index >= argc) {
                return failure(std::string("--runtime requires a value"));
            }
            options.runtime_executable = argv[index];
            continue;
        }
        if (argument == "--external-runtime") {
            options.external_runtime = true;
            continue;
        }
        if (argument == "--") {
            for (++index; index < argc; ++index) {
                options.runtime_command.emplace_back(argv[index]);
            }
            break;
        }
        return failure(
            std::string("Unknown fei-agentd argument: ") + std::string(argument)
        );
    }
    if (!options.show_help && options.project_file.empty()) {
        return failure(std::string("--project is required"));
    }
    if (options.external_runtime &&
        (options.runtime_executable || !options.runtime_command.empty())) {
        return failure(
            std::string(
                "--external-runtime cannot be combined with a runtime command"
            )
        );
    }
    if (options.runtime_executable && !options.runtime_command.empty()) {
        return failure(
            std::string("--runtime cannot be combined with a command after --")
        );
    }
    return options;
}

std::string default_runtime_executable(std::string_view agentd_executable) {
#if defined(_WIN32)
    constexpr std::string_view c_runtime_name = "fei-runtime-host.exe";
#else
    constexpr std::string_view c_runtime_name = "fei-runtime-host";
#endif
    const std::filesystem::path agentd_path {agentd_executable};
    if (agentd_path.has_parent_path()) {
        std::error_code error;
        const auto absolute_agentd =
            std::filesystem::absolute(agentd_path, error);
        if (!error) {
            const auto sibling = absolute_agentd.parent_path() / c_runtime_name;
            if (std::filesystem::is_regular_file(sibling, error) && !error) {
                return sibling.string();
            }
        }
    }
    return std::string(c_runtime_name);
}

void configure_runtime_command(
    Options& options,
    const ProjectDescriptor& project,
    std::string_view agentd_executable
) {
    if (options.external_runtime || !options.runtime_command.empty()) {
        return;
    }
    options.runtime_command = {
        options.runtime_executable.value_or(
            default_runtime_executable(agentd_executable)
        ),
        project.project_file.string(),
    };
}

std::string make_session() {
    std::random_device source;
    std::mt19937_64 random(source());
    constexpr char digits[] = "0123456789abcdef";
    std::string session(32, '0');
    for (auto& value : session) {
        value = digits[random() & 0x0f];
    }
    return session;
}

Status<std::string> start_runtime(
    RuntimeProcess& process,
    SupervisorState& state,
    const Options& options
) {
    auto status = process.start(
        ProcessLaunch {
            .arguments = options.runtime_command,
            .environment = {
                {"FEI_AGENTD_PORT", std::to_string(options.port)},
                {"FEI_RUNTIME_SESSION", state.session()},
            },
        }
    );
    if (!status) {
        return status;
    }
    state.mark_process_started(process.process_id());
    info("Started runtime process {}", process.process_id());
    return {};
}

} // namespace

int main(int argc, char** argv) {
    auto options = parse_options(argc, argv);
    if (!options) {
        fei::error("{}", options.error());
        print_help();
        return 1;
    }
    if (options->show_help) {
        print_help();
        return 0;
    }

    auto project = Project::load(options->project_file);
    if (!project) {
        fei::error(
            "Failed to load project '{}': {}",
            project.error().path.string(),
            project.error().message
        );
        return 1;
    }
    auto project_descriptor = describe_project(*project);
    configure_runtime_command(*options, project_descriptor, argv[0]);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    SupervisorState state(make_session(), project_descriptor);
    AgentServer server(state, options->port);
    if (auto status = server.start(); !status) {
        fei::error("{}", status.error());
        return 1;
    }
    info("fei-agentd listening at http://127.0.0.1:{}", options->port);
    info(
        "Bound to project '{}' at {}",
        project_descriptor.name,
        project_descriptor.project_file.string()
    );
    info("Runtime session: {}", state.session());

    RuntimeProcess runtime;
    if (!options->external_runtime) {
        if (auto status = start_runtime(runtime, state, *options); !status) {
            fei::error("{}", status.error());
            server.stop();
            return 1;
        }
    } else {
        info(
            "Waiting for an external runtime; set FEI_AGENTD_PORT={} and "
            "FEI_RUNTIME_SESSION={} when launching one manually",
            options->port,
            state.session()
        );
    }

    while (running.load(std::memory_order_relaxed)) {
        if (auto exit_code = runtime.poll()) {
            state.mark_process_exited(*exit_code);
            warn("Runtime process exited with code {}", *exit_code);
        }
        if (state.consume_restart_request()) {
            if (options->external_runtime) {
                warn("Cannot restart a runtime that agentd did not launch");
            } else {
                runtime.terminate();
                if (auto status = start_runtime(runtime, state, *options);
                    !status) {
                    fei::error("Failed to restart runtime: {}", status.error());
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    runtime.terminate();
    server.stop();
    return 0;
}
