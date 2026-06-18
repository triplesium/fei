#include "codegen.hpp"
#include "model.hpp"
#include "parser.hpp"

#include <algorithm>
#include <chrono>
#include <CLI/CLI.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::vector<std::string> headers;
    std::vector<std::string> includes;
    std::vector<std::string> registrars;
    std::filesystem::path root_dir = std::filesystem::current_path();
    std::filesystem::path output_file;
    std::filesystem::path stamp_file;
    std::string function_name = "register_reflection";
    bool aggregate = false;
    bool verbose = false;
};

[[nodiscard]] bool is_header_path(std::string_view path) {
    const auto ext = std::filesystem::path(path).extension().string();
    return ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx";
}

void configure_options(CLI::App& app, Options& options) {
    app.add_option("headers", options.headers, "Header files to parse");
    auto* include_option = app.add_option_function<std::string>(
        "-I,--include",
        [&options](const std::string& include) {
            options.includes.push_back(include);
        },
        "Include directory"
    );
    include_option->trigger_on_parse();
    app.add_flag("-v,--verbose", options.verbose, "Enable verbose output");
    app.add_flag("--aggregate", options.aggregate, "Generate aggregate output");
    app.add_option(
        "--rootdir",
        options.root_dir,
        "Root directory for relative paths"
    );
    app.add_option("-o,--output", options.output_file, "Output C++ file");
    app.add_option(
        "--function",
        options.function_name,
        "Generated registration function name"
    );
    auto* registrar_option = app.add_option_function<std::string>(
        "--registrar",
        [&options](const std::string& registrar) {
            options.registrars.push_back(registrar);
        },
        "Registration function to call from aggregate output"
    );
    registrar_option->trigger_on_parse();
    app.add_option("--stamp", options.stamp_file, "Stamp file to write");
}

void normalize_options(Options& options) {
    options.headers.erase(
        std::remove_if(
            options.headers.begin(),
            options.headers.end(),
            [](const std::string& header) {
                return !is_header_path(header);
            }
        ),
        options.headers.end()
    );

    if (options.headers.empty() && !options.aggregate) {
        throw std::runtime_error("no headers provided");
    }
    if (options.aggregate && options.output_file.empty()) {
        throw std::runtime_error("aggregate output file is required");
    }
}

void write_stamp_file(const std::filesystem::path& stamp_file) {
    if (stamp_file.empty()) {
        return;
    }
    if (stamp_file.has_parent_path()) {
        std::filesystem::create_directories(stamp_file.parent_path());
    }

    std::ofstream out(stamp_file, std::ios::binary);
    if (!out) {
        throw std::runtime_error(
            "failed to open stamp file: " + stamp_file.string()
        );
    }
    out << std::chrono::system_clock::now().time_since_epoch().count() << '\n';
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    CLI::App app {"Generate C++ reflection metadata"};
    configure_options(app, options);
    CLI11_PARSE(app, argc, argv);

    try {
        normalize_options(options);

        if (options.aggregate) {
            fei::reflgen::generate_aggregate_cpp_file(
                options.registrars,
                options.output_file
            );
            write_stamp_file(options.stamp_file);
            std::cout << "Generated C++ reflection aggregate: "
                      << options.output_file.generic_string() << '\n';
            return 0;
        }

        fei::reflgen::HeaderParser parser(
            options.headers,
            options.includes,
            options.verbose
        );
        auto result = parser.parse();
        fei::reflgen::dedupe_reflected_types(result);
        fei::reflgen::filter_codegen_unsupported_members(result);

        std::cout << "\nParsing complete! Found " << result.classes.size()
                  << " classes and " << result.enums.size() << " enums in "
                  << options.headers.size() << " files.\n";

        if (result.classes.empty() && result.enums.empty()) {
            std::cout << "No classes or enums found in the header files.\n";
        }

        if (!options.output_file.empty()) {
            fei::reflgen::generate_cpp_file(
                result,
                options.root_dir,
                options.output_file,
                options.function_name
            );
            write_stamp_file(options.stamp_file);
            std::cout << "Generated C++ reflection code: "
                      << options.output_file.generic_string() << '\n';
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
