#include "play_runner.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using Json = nlohmann::json;
using JsonResult = ets::Result<Json, std::string>;

struct PlayTraceSession {
    std::string trace_id;
    std::string span_id;
    std::optional<std::size_t> active_call_index;
};

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
    std::optional<std::string> interface_id;
    std::optional<std::string> output;
    std::optional<std::string> input;
    std::optional<std::string> eval;
    std::optional<std::string> checkpoint_name;
    std::optional<std::uint32_t> entity;
    bool read_stdin {false};
    bool strict {false};
    bool show_help {false};
};

void print_help() {
    std::cout
        << "usage: entisium-ctl [--port PORT] "
           "project|status|capabilities|watch|restart|play-interfaces|"
           "play-reset\n"
        << "       entisium-ctl [--port PORT] inspect PROVIDER "
           "[--schema SCHEMA] --payload JSON\n"
        << "       entisium-ctl [--port PORT] inspect ecs.entity.inspect "
           "--entity ENTITY\n"
        << "       entisium-ctl [--port PORT] play-capture --output FILE\n"
        << "       entisium-ctl [--port PORT] play-observe --interface "
           "INTERFACE\n"
        << "       entisium-ctl [--port PORT] play-step --payload JSON\n"
        << "       entisium-ctl [--port PORT] play-run --eval CODE\n"
        << "       entisium-ctl [--port PORT] play-run --stdin\n"
        << "       entisium-ctl [--port PORT] checkpoint-create NAME "
           "[--strict]\n"
        << "       entisium-ctl [--port PORT] checkpoint-audit\n"
        << "       entisium-ctl [--port PORT] checkpoint-delete NAME\n"
        << "       entisium-ctl [--port PORT] checkpoint-clear\n"
        << "       entisium-ctl [--port PORT] checkpoint-export NAME "
           "--output FILE\n"
        << "       entisium-ctl [--port PORT] checkpoint-import NAME "
           "--input FILE\n"
        << "       entisium-ctl [--port PORT] checkpoint-list\n"
        << "       entisium-ctl [--port PORT] checkpoint-restore NAME\n";
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
        if (argument == "--interface") {
            if (++index >= argc) {
                std::cerr << "--interface requires a value\n";
                return false;
            }
            options.interface_id = argv[index];
            continue;
        }
        if (argument == "--output") {
            if (++index >= argc) {
                std::cerr << "--output requires a value\n";
                return false;
            }
            options.output = argv[index];
            continue;
        }
        if (argument == "--input") {
            if (++index >= argc) {
                std::cerr << "--input requires a value\n";
                return false;
            }
            options.input = argv[index];
            continue;
        }
        if (argument == "--eval") {
            if (++index >= argc) {
                std::cerr << "--eval requires a value\n";
                return false;
            }
            options.eval = argv[index];
            continue;
        }
        if (argument == "--stdin") {
            options.read_stdin = true;
            continue;
        }
        if (argument == "--strict") {
            options.strict = true;
            continue;
        }
        if (!command_seen &&
            (argument == "project" || argument == "status" ||
             argument == "watch" || argument == "restart" ||
             argument == "capabilities" || argument == "inspect" ||
             argument == "play-interfaces" || argument == "play-capture" ||
             argument == "play-observe" || argument == "play-step" ||
             argument == "play-reset" || argument == "play-run" ||
             argument == "checkpoint-create" ||
             argument == "checkpoint-audit" ||
             argument == "checkpoint-delete" ||
             argument == "checkpoint-export" ||
             argument == "checkpoint-import" ||
             argument == "checkpoint-clear" || argument == "checkpoint-list" ||
             argument == "checkpoint-restore")) {
            options.command = argument;
            command_seen = true;
            continue;
        }
        if (options.command == "inspect" && options.provider.empty()) {
            options.provider = argument;
            continue;
        }
        if ((options.command == "checkpoint-create" ||
             options.command == "checkpoint-restore" ||
             options.command == "checkpoint-delete" ||
             options.command == "checkpoint-export" ||
             options.command == "checkpoint-import") &&
            !options.checkpoint_name) {
            options.checkpoint_name = argument;
            continue;
        }
        {
            std::cerr << "Unknown argument: " << argument << '\n';
            return false;
        }
    }
    if (options.show_help) {
        return true;
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
        if (options.interface_id || options.output || options.input ||
            options.eval || options.read_stdin) {
            std::cerr << "Unexpected inspect option\n";
            return false;
        }
    } else if (options.command == "play-capture") {
        if (!options.output) {
            std::cerr << "play-capture requires --output\n";
            return false;
        }
        if (options.entity || options.payload || options.schema ||
            options.interface_id || options.input || options.eval ||
            options.read_stdin) {
            std::cerr << "play-capture accepts only --output\n";
            return false;
        }
    } else if (options.command == "play-observe") {
        if (!options.interface_id) {
            std::cerr << "play-observe requires --interface\n";
            return false;
        }
        if (options.entity || options.payload || options.schema ||
            options.output || options.input || options.eval ||
            options.read_stdin) {
            std::cerr << "play-observe accepts only --interface\n";
            return false;
        }
    } else if (options.command == "play-step") {
        if (!options.payload) {
            std::cerr << "play-step requires --payload\n";
            return false;
        }
        if (options.entity || options.schema || options.interface_id ||
            options.output || options.input || options.eval ||
            options.read_stdin) {
            std::cerr << "play-step accepts only --payload\n";
            return false;
        }
    } else if (options.command == "play-run") {
        if (options.eval.has_value() == options.read_stdin) {
            std::cerr << "play-run requires exactly one of --eval or --stdin\n";
            return false;
        }
        if (options.entity || options.payload || options.schema ||
            options.interface_id || options.output || options.input) {
            std::cerr << "play-run accepts only --eval or --stdin\n";
            return false;
        }
    } else if (
        options.command == "checkpoint-export" ||
        options.command == "checkpoint-import"
    ) {
        if (!options.checkpoint_name) {
            std::cerr << options.command << " requires a checkpoint name\n";
            return false;
        }
        const auto export_command = options.command == "checkpoint-export";
        if ((export_command && !options.output) ||
            (!export_command && !options.input)) {
            std::cerr << options.command << " requires "
                      << (export_command ? "--output" : "--input") << '\n';
            return false;
        }
        if (options.entity || options.payload || options.schema ||
            options.interface_id || options.eval || options.read_stdin ||
            options.strict || (export_command && options.input) ||
            (!export_command && options.output)) {
            std::cerr << options.command
                      << " accepts only a checkpoint name and "
                      << (export_command ? "--output" : "--input") << '\n';
            return false;
        }
    } else if (
        options.command == "checkpoint-create" ||
        options.command == "checkpoint-restore" ||
        options.command == "checkpoint-delete"
    ) {
        if (!options.checkpoint_name) {
            std::cerr << options.command << " requires a checkpoint name\n";
            return false;
        }
        if (options.entity || options.payload || options.schema ||
            options.interface_id || options.output || options.input ||
            options.eval || options.read_stdin) {
            std::cerr << options.command << " accepts only a checkpoint name\n";
            return false;
        }
        if (options.strict && options.command != "checkpoint-create") {
            std::cerr << "--strict is only valid for checkpoint-create\n";
            return false;
        }
    } else if (
        options.command == "checkpoint-list" ||
        options.command == "checkpoint-audit" ||
        options.command == "checkpoint-clear"
    ) {
        if (options.entity || options.payload || options.schema ||
            options.interface_id || options.output || options.input ||
            options.eval || options.read_stdin || options.checkpoint_name) {
            std::cerr << options.command << " accepts no options\n";
            return false;
        }
        if (options.strict) {
            std::cerr << options.command << " does not accept --strict\n";
            return false;
        }
    } else if (
        options.entity || options.payload || options.schema ||
        options.interface_id || options.output || options.input ||
        options.eval || options.read_stdin || options.checkpoint_name ||
        options.strict
    ) {
        std::cerr << "Unexpected command option\n";
        return false;
    }
    return true;
}

void configure_client(httplib::Client& client) {
    client.set_connection_timeout(0, 500000);
    client.set_read_timeout(35, 0);
    client.set_write_timeout(1, 0);
}

bool print_status(httplib::Client& client, std::string* previous = nullptr) {
    auto response = client.Get("/api/v1/status");
    if (!response) {
        std::cerr << "Failed to connect to entisium-agentd\n";
        return false;
    }
    if (response->status != 200) {
        std::cerr << "entisium-agentd returned HTTP " << response->status
                  << '\n';
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
        std::cerr << "Failed to connect to entisium-agentd\n";
        return false;
    }
    if (response->status != 200) {
        std::cerr << "entisium-agentd returned HTTP " << response->status
                  << '\n';
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
        std::cerr << "Failed to connect to entisium-agentd\n";
        return std::nullopt;
    }
    if (response->status != 200) {
        std::cerr << "entisium-agentd returned HTTP " << response->status
                  << '\n';
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
        std::cerr << "Failed to connect to entisium-agentd\n";
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

bool inspect_checkpoint(
    httplib::Client& client,
    std::string provider,
    const std::optional<std::string>& name,
    bool strict = false
) {
    Options request;
    request.command = "inspect";
    request.provider = std::move(provider);
    request.payload = name ?
                          std::optional<std::string>(
                              Json {{"name", *name}, {"strict", strict}}.dump()
                          ) :
                          std::optional<std::string>("{}");
    return inspect_runtime(client, request);
}

bool inspect_checkpoint_file(
    httplib::Client& client,
    std::string provider,
    const std::string& name,
    const std::string& path
) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        std::cerr << "Failed to resolve checkpoint archive path: "
                  << error.message() << '\n';
        return false;
    }
    absolute = absolute.lexically_normal();

    Options request;
    request.command = "inspect";
    request.provider = std::move(provider);
    request.payload =
        Json {
            {"name", name},
            {"path", absolute.generic_string()},
        }
            .dump();
    return inspect_runtime(client, request);
}

bool print_response(httplib::Result response, std::string_view failure) {
    if (!response) {
        std::cerr << failure << '\n';
        return false;
    }
    try {
        std::cout << nlohmann::json::parse(response->body).dump(2) << '\n';
    } catch (const std::exception&) {
        std::cout << response->body << '\n';
    }
    return response->status >= 200 && response->status < 300;
}

std::string response_error(const Json& document, std::string fallback) {
    if (!document.is_object()) {
        return fallback;
    }
    if (auto message = document.find("message");
        message != document.end() && message->is_string()) {
        return message->get<std::string>();
    }
    if (auto error = document.find("error");
        error != document.end() && error->is_object()) {
        if (auto message = error->find("message");
            message != error->end() && message->is_string()) {
            return message->get<std::string>();
        }
    }
    return fallback;
}

JsonResult
parse_json_response(httplib::Result response, std::string_view failure) {
    if (!response) {
        return ets::failure(std::string(failure));
    }
    Json document;
    try {
        document = Json::parse(response->body);
    } catch (const std::exception& error) {
        return ets::failure(
            std::string(failure) + ": invalid JSON response: " + error.what()
        );
    }
    if (response->status < 200 || response->status >= 300) {
        return ets::failure(response_error(
            document,
            std::string(failure) + ": HTTP " + std::to_string(response->status)
        ));
    }
    return document;
}

JsonResult
inspection_payload(httplib::Result response, std::string_view failure) {
    auto document = parse_json_response(std::move(response), failure);
    if (!document) {
        return ets::failure(std::move(document.error()));
    }
    if (!document->is_object() || !document->value("ok", false)) {
        return ets::failure(response_error(*document, std::string(failure)));
    }
    if (!document->contains("payload")) {
        return ets::failure(std::string(failure) + ": response has no payload");
    }
    return std::move(document->at("payload"));
}

httplib::Headers trace_headers(const PlayTraceSession* trace) {
    if (trace == nullptr) {
        return {};
    }
    httplib::Headers headers {
        {"X-Entisium-Trace-Id", trace->trace_id},
        {"X-Entisium-Parent-Span-Id", trace->span_id},
    };
    if (trace->active_call_index) {
        headers.emplace(
            "X-Entisium-Call-Index",
            std::to_string(*trace->active_call_index)
        );
    }
    return headers;
}

std::optional<PlayTraceSession>
start_play_trace(httplib::Client& client, std::string_view source) {
    auto response = parse_json_response(
        client.Post(
            "/api/v1/play/traces",
            Json {{"source", source}, {"origin", "entisium-ctl play-run"}}
                .dump(),
            "application/json"
        ),
        "Failed to start play trace"
    );
    if (!response) {
        std::cerr << "Play trace unavailable: " << response.error() << '\n';
        return std::nullopt;
    }
    try {
        return PlayTraceSession {
            .trace_id = response->at("trace_id").get<std::string>(),
            .span_id = response->at("span_id").get<std::string>(),
        };
    } catch (const std::exception& error) {
        std::cerr << "Play trace unavailable: invalid response: "
                  << error.what() << '\n';
        return std::nullopt;
    }
}

void publish_play_log(
    httplib::Client& client,
    const PlayTraceSession& trace,
    const Json& value
) {
    const auto path = "/api/v1/play/traces/" + trace.trace_id + "/events";
    (void)client.Post(
        path,
        Json {
            {"type", "play_log"},
            {"parent_span_id", trace.span_id},
            {"data", value},
        }
            .dump(),
        "application/json"
    );
}

void finish_play_trace(
    httplib::Client& client,
    const PlayTraceSession& trace,
    const Json& report
) {
    const auto path = "/api/v1/play/traces/" + trace.trace_id + "/finish";
    (void)client.Post(
        path,
        Json {{"span_id", trace.span_id}, {"report", report}}.dump(),
        "application/json"
    );
}

JsonResult request_capture(
    httplib::Client& client,
    const std::optional<std::string>& output,
    const PlayTraceSession* trace = nullptr
) {
    auto response = client.Post(
        "/api/v1/play/capture",
        trace_headers(trace),
        "{}",
        "application/json"
    );
    auto parsed = parse_json_response(
        std::move(response),
        "Failed to capture playtest frame"
    );
    if (!parsed) {
        return ets::failure(std::move(parsed.error()));
    }

    try {
        auto metadata = std::move(*parsed);
        const auto artifact = metadata.at("artifact").get<std::string>();
        const auto expected_bytes = metadata.at("bytes").get<std::size_t>();
        if (metadata.at("content_type").get<std::string_view>() !=
            "image/png") {
            return ets::failure(
                std::string("Capture metadata is not for a PNG artifact")
            );
        }
        if (!std::string_view(artifact).starts_with("/api/v1/artifacts/")) {
            return ets::failure(
                std::string("Invalid artifact URL returned by entisium-agentd")
            );
        }
        if (!output) {
            return metadata;
        }
        auto download = client.Get(artifact);
        if (!download || download->status != 200) {
            return ets::failure(
                std::string("Failed to download captured frame artifact")
            );
        }
        constexpr std::string_view c_png_signature {"\x89PNG\r\n\x1a\n", 8};
        if (download->get_header_value("Content-Type") != "image/png" ||
            download->body.size() != expected_bytes ||
            !std::string_view(download->body).starts_with(c_png_signature)) {
            return ets::failure(
                std::string("Downloaded capture artifact is invalid")
            );
        }

        std::ofstream file(*output, std::ios::binary | std::ios::trunc);
        if (!file) {
            return ets::failure(
                std::string("Failed to open capture output: ") + *output
            );
        }
        file.write(
            download->body.data(),
            static_cast<std::streamsize>(download->body.size())
        );
        if (!file) {
            return ets::failure(
                std::string("Failed to write capture output: ") + *output
            );
        }
        metadata["output"] = *output;
        return metadata;
    } catch (const std::exception& error) {
        return ets::failure(
            std::string("Invalid capture response: ") + error.what()
        );
    }
}

bool capture_frame(httplib::Client& client, const Options& options) {
    auto capture = request_capture(client, options.output);
    if (!capture) {
        std::cerr << capture.error() << '\n';
        return false;
    }
    std::cout << capture->dump(2) << '\n';
    return true;
}

ets::agentd::PlayControlBindings
make_play_bindings(httplib::Client& client, PlayTraceSession* trace = nullptr) {
    return ets::agentd::PlayControlBindings {
        .interfaces = [&client, trace]() -> JsonResult {
            return inspection_payload(
                client.Get("/api/v1/play/interfaces", trace_headers(trace)),
                "Failed to discover playtest interfaces"
            );
        },
        .step = [&client, trace](
                    std::string_view interface_id,
                    const Json& action,
                    std::optional<ets::uint32> ticks
                ) -> JsonResult {
            Json request {
                {"interface", interface_id},
                {"action", action},
            };
            if (ticks) {
                request["ticks"] = *ticks;
            }
            return inspection_payload(
                client.Post(
                    "/api/v1/play/step",
                    trace_headers(trace),
                    request.dump(),
                    "application/json"
                ),
                "Failed to execute playtest step"
            );
        },
        .observe = [&client,
                    trace](std::string_view interface_id) -> JsonResult {
            const auto request = Json {{"interface", interface_id}}.dump();
            return inspection_payload(
                client.Post(
                    "/api/v1/play/observe",
                    trace_headers(trace),
                    request,
                    "application/json"
                ),
                "Failed to observe playtest state"
            );
        },
        .capture = [&client, trace](const std::optional<std::string>& output)
            -> JsonResult {
            return request_capture(client, output, trace);
        },
        .checkpoint =
            [&client, trace](std::string_view name, bool strict) -> JsonResult {
            return inspection_payload(
                client.Post(
                    "/api/v1/play/checkpoint",
                    trace_headers(trace),
                    Json {{"name", name}, {"strict", strict}}.dump(),
                    "application/json"
                ),
                "Failed to create playtest checkpoint"
            );
        },
        .restore = [&client, trace](std::string_view name) -> JsonResult {
            return inspection_payload(
                client.Post(
                    "/api/v1/play/restore",
                    trace_headers(trace),
                    Json {{"name", name}}.dump(),
                    "application/json"
                ),
                "Failed to restore playtest checkpoint"
            );
        },
    };
}

bool run_play_script(httplib::Client& client, const Options& options) {
    std::string source;
    if (options.eval) {
        source = *options.eval;
    } else {
        source.assign(
            std::istreambuf_iterator<char>(std::cin),
            std::istreambuf_iterator<char>()
        );
        if (std::cin.bad()) {
            std::cerr << "Failed to read play-run source from stdin\n";
            return false;
        }
    }

    auto trace = start_play_trace(client, source);
    const auto observer = ets::agentd::PlayRunObserver {
        .call_started =
            [&trace](std::size_t index, std::string_view, const Json&) {
                if (trace) {
                    trace->active_call_index = index;
                }
            },
        .call_finished =
            [&trace](std::size_t index) {
                if (trace && trace->active_call_index == index) {
                    trace->active_call_index.reset();
                }
            },
        .log =
            [&client, &trace](const Json& value) {
                if (trace) {
                    publish_play_log(client, *trace, value);
                }
            },
    };
    const auto report = ets::agentd::run_luau_play_script(
        source,
        make_play_bindings(client, trace ? &*trace : nullptr),
        {},
        observer
    );
    if (trace) {
        finish_play_trace(client, *trace, report);
    }
    std::cout << report.dump(2) << '\n';
    return report.value("ok", false);
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
    if (options.command == "checkpoint-create") {
        return inspect_checkpoint(
                   client,
                   "play.checkpoint.create",
                   options.checkpoint_name,
                   options.strict
               ) ?
                   0 :
                   1;
    }
    if (options.command == "checkpoint-list") {
        return inspect_checkpoint(
                   client,
                   "play.checkpoint.list",
                   std::nullopt
               ) ?
                   0 :
                   1;
    }
    if (options.command == "checkpoint-export") {
        return inspect_checkpoint_file(
                   client,
                   "play.checkpoint.export",
                   *options.checkpoint_name,
                   *options.output
               ) ?
                   0 :
                   1;
    }
    if (options.command == "checkpoint-import") {
        return inspect_checkpoint_file(
                   client,
                   "play.checkpoint.import",
                   *options.checkpoint_name,
                   *options.input
               ) ?
                   0 :
                   1;
    }
    if (options.command == "checkpoint-audit") {
        return inspect_checkpoint(
                   client,
                   "play.checkpoint.audit",
                   std::nullopt
               ) ?
                   0 :
                   1;
    }
    if (options.command == "checkpoint-delete") {
        return inspect_checkpoint(
                   client,
                   "play.checkpoint.delete",
                   options.checkpoint_name
               ) ?
                   0 :
                   1;
    }
    if (options.command == "checkpoint-clear") {
        return inspect_checkpoint(
                   client,
                   "play.checkpoint.clear",
                   std::nullopt
               ) ?
                   0 :
                   1;
    }
    if (options.command == "checkpoint-restore") {
        return inspect_checkpoint(
                   client,
                   "play.checkpoint.restore",
                   options.checkpoint_name
               ) ?
                   0 :
                   1;
    }
    if (options.command == "play-interfaces") {
        return print_response(
                   client.Get("/api/v1/play/interfaces"),
                   "Failed to discover playtest interfaces"
               ) ?
                   0 :
                   1;
    }
    if (options.command == "play-capture") {
        return capture_frame(client, options) ? 0 : 1;
    }
    if (options.command == "play-observe") {
        const auto body =
            nlohmann::json {{"interface", *options.interface_id}}.dump();
        return print_response(
                   client
                       .Post("/api/v1/play/observe", body, "application/json"),
                   "Failed to observe playtest state"
               ) ?
                   0 :
                   1;
    }
    if (options.command == "play-step") {
        return print_response(
                   client.Post(
                       "/api/v1/play/step",
                       *options.payload,
                       "application/json"
                   ),
                   "Failed to execute playtest step"
               ) ?
                   0 :
                   1;
    }
    if (options.command == "play-run") {
        return run_play_script(client, options) ? 0 : 1;
    }
    if (options.command == "play-reset") {
        return print_response(
                   client.Post("/api/v1/play/reset", "", "application/json"),
                   "Failed to reset playtest runtime"
               ) ?
                   0 :
                   1;
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
