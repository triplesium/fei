#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::atomic<bool> running {true};

void handle_signal(int) {
    running.store(false, std::memory_order_relaxed);
}

struct Options {
    std::uint16_t port {8091};
    std::string command {"status"};
    std::string provider;
    std::optional<std::string> schema;
    std::optional<std::string> payload;
    std::optional<std::uint32_t> entity;
    bool show_help {false};
};

void print_help() {
    std::cout << "usage: fei-ctl [--port PORT] "
                 "project|status|capabilities|watch|restart\n"
              << "       fei-ctl [--port PORT] inspect PROVIDER "
                 "[--schema SCHEMA] --payload JSON\n"
              << "       fei-ctl [--port PORT] inspect ecs.entity.inspect "
                 "--entity ENTITY\n";
}

bool parse_options(int argc, char** argv, Options& options) {
    bool command_seen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            continue;
        }
        if (argument == "--port") {
            if (++index >= argc) {
                std::cerr << "--port requires a value\n";
                return false;
            }
            std::uint16_t port {};
            const std::string_view value {argv[index]};
            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                port
            );
            if (error != std::errc {} || end != value.data() + value.size() ||
                port == 0) {
                std::cerr << "Invalid port: " << value << '\n';
                return false;
            }
            options.port = port;
            continue;
        }
        if (argument == "--entity") {
            if (++index >= argc) {
                std::cerr << "--entity requires a value\n";
                return false;
            }
            std::uint32_t entity {};
            const std::string_view value {argv[index]};
            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                entity
            );
            if (error != std::errc {} || end != value.data() + value.size()) {
                std::cerr << "Invalid entity: " << value << '\n';
                return false;
            }
            options.entity = entity;
            continue;
        }
        if (argument == "--schema") {
            if (++index >= argc) {
                std::cerr << "--schema requires a value\n";
                return false;
            }
            options.schema = argv[index];
            continue;
        }
        if (argument == "--payload") {
            if (++index >= argc) {
                std::cerr << "--payload requires a value\n";
                return false;
            }
            options.payload = argv[index];
            continue;
        }
        if (!command_seen &&
            (argument == "project" || argument == "status" ||
             argument == "watch" || argument == "restart" ||
             argument == "capabilities" || argument == "inspect")) {
            options.command = argument;
            command_seen = true;
            continue;
        }
        if (options.command == "inspect" && options.provider.empty()) {
            options.provider = argument;
            continue;
        }
        {
            std::cerr << "Unknown argument: " << argument << '\n';
            return false;
        }
    }
    if (options.command == "inspect") {
        if (options.provider.empty()) {
            std::cerr << "inspect requires a provider\n";
            return false;
        }
        if (options.entity && options.payload) {
            std::cerr << "inspect accepts either --entity or --payload\n";
            return false;
        }
        if (!options.entity && !options.payload) {
            std::cerr << "inspect requires --entity or --payload\n";
            return false;
        }
    } else if (options.entity || options.payload || options.schema) {
        std::cerr << "--entity, --payload, and --schema require inspect\n";
        return false;
    }
    return true;
}

void configure_client(httplib::Client& client) {
    client.set_connection_timeout(0, 500000);
    client.set_read_timeout(6, 0);
    client.set_write_timeout(1, 0);
}

bool print_status(httplib::Client& client, std::string* previous = nullptr) {
    auto response = client.Get("/api/v1/status");
    if (!response) {
        std::cerr << "Failed to connect to fei-agentd\n";
        return false;
    }
    if (response->status != 200) {
        std::cerr << "fei-agentd returned HTTP " << response->status << '\n';
        return false;
    }
    if (previous && *previous == response->body) {
        return true;
    }
    try {
        std::cout << nlohmann::json::parse(response->body).dump(2) << '\n';
    } catch (const std::exception&) {
        std::cout << response->body << '\n';
    }
    if (previous) {
        *previous = response->body;
    }
    return true;
}

bool print_project(httplib::Client& client) {
    auto response = client.Get("/api/v1/project");
    if (!response) {
        std::cerr << "Failed to connect to fei-agentd\n";
        return false;
    }
    if (response->status != 200) {
        std::cerr << "fei-agentd returned HTTP " << response->status << '\n';
        return false;
    }
    try {
        std::cout << nlohmann::json::parse(response->body).dump(2) << '\n';
    } catch (const std::exception&) {
        std::cout << response->body << '\n';
    }
    return true;
}

std::optional<nlohmann::json>
get_capabilities(httplib::Client& client, bool print) {
    auto response = client.Get("/api/v1/capabilities");
    if (!response) {
        std::cerr << "Failed to connect to fei-agentd\n";
        return std::nullopt;
    }
    if (response->status != 200) {
        std::cerr << "fei-agentd returned HTTP " << response->status << '\n';
        return std::nullopt;
    }
    try {
        auto document = nlohmann::json::parse(response->body);
        if (print) {
            std::cout << document.dump(2) << '\n';
        }
        return document;
    } catch (const std::exception& error) {
        std::cerr << "Invalid capabilities response: " << error.what() << '\n';
        return std::nullopt;
    }
}

std::optional<std::string>
discover_schema(httplib::Client& client, std::string_view provider) {
    auto capabilities = get_capabilities(client, false);
    if (!capabilities) {
        return std::nullopt;
    }
    try {
        for (const auto& inspection : capabilities->at("inspections")) {
            if (inspection.at("id").get<std::string_view>() == provider) {
                return inspection.at("schema").get<std::string>();
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Invalid capabilities response: " << error.what() << '\n';
        return std::nullopt;
    }
    std::cerr << "Runtime does not advertise inspection provider '" << provider
              << "'\n";
    return std::nullopt;
}

bool inspect_runtime(httplib::Client& client, const Options& options) {
    auto schema = options.schema ? options.schema :
                                   discover_schema(client, options.provider);
    if (!schema) {
        return false;
    }

    nlohmann::json payload;
    if (options.entity) {
        payload = nlohmann::json {{"entity", *options.entity}};
    } else {
        try {
            payload = nlohmann::json::parse(*options.payload);
        } catch (const std::exception& error) {
            std::cerr << "Invalid --payload JSON: " << error.what() << '\n';
            return false;
        }
    }
    const auto body =
        nlohmann::json {
            {"provider", options.provider},
            {"schema", *schema},
            {"payload", std::move(payload)},
            {"timeout_ms", 5000},
        }
            .dump();
    auto response = client.Post("/api/v1/inspection", body, "application/json");
    if (!response) {
        std::cerr << "Failed to connect to fei-agentd\n";
        return false;
    }
    try {
        const auto document = nlohmann::json::parse(response->body);
        std::cout << document.dump(2) << '\n';
        return response->status == 200 && document.value("ok", false);
    } catch (const std::exception&) {
        std::cout << response->body << '\n';
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_help();
        return 1;
    }
    if (options.show_help) {
        print_help();
        return 0;
    }

    httplib::Client client("127.0.0.1", options.port);
    configure_client(client);

    if (options.command == "project") {
        return print_project(client) ? 0 : 1;
    }
    if (options.command == "status") {
        return print_status(client) ? 0 : 1;
    }
    if (options.command == "restart") {
        auto response = client.Post("/api/v1/restart", "", "application/json");
        if (!response || response->status != 200) {
            std::cerr << "Failed to request runtime restart\n";
            return 1;
        }
        std::cout << "Runtime restart requested\n";
        return 0;
    }
    if (options.command == "capabilities") {
        return get_capabilities(client, true) ? 0 : 1;
    }
    if (options.command == "inspect") {
        return inspect_runtime(client, options) ? 0 : 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::string previous;
    while (running.load(std::memory_order_relaxed)) {
        (void)print_status(client, &previous);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return 0;
}
