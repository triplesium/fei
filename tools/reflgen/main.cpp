#include "codegen.hpp"
#include "model.hpp"
#include "parser.hpp"

#include <algorithm>
#include <chrono>
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

[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) {
    return text.starts_with(prefix);
}

[[nodiscard]] std::string value_after_equals(std::string_view arg) {
    const auto pos = arg.find('=');
    if (pos == std::string_view::npos) {
        return {};
    }
    return std::string(arg.substr(pos + 1));
}

[[nodiscard]] std::string
require_next(int& index, int argc, char** argv, std::string_view option_name) {
    if (index + 1 >= argc) {
        throw std::runtime_error(
            "missing value for " + std::string(option_name)
        );
    }
    ++index;
    return argv[index];
}

[[nodiscard]] bool is_header_path(std::string_view path) {
    const auto ext = std::filesystem::path(path).extension().string();
    return ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx";
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

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg == "--aggregate") {
            options.aggregate = true;
        } else if (arg == "--rootdir") {
            options.root_dir = require_next(i, argc, argv, arg);
        } else if (starts_with(arg, "--rootdir=")) {
            options.root_dir = value_after_equals(arg);
        } else if (arg == "--output" || arg == "-o") {
            options.output_file = require_next(i, argc, argv, arg);
        } else if (starts_with(arg, "--output=")) {
            options.output_file = value_after_equals(arg);
        } else if (arg == "--function") {
            options.function_name = require_next(i, argc, argv, arg);
        } else if (starts_with(arg, "--function=")) {
            options.function_name = value_after_equals(arg);
        } else if (arg == "--registrar") {
            options.registrars.push_back(require_next(i, argc, argv, arg));
        } else if (starts_with(arg, "--registrar=")) {
            options.registrars.push_back(value_after_equals(arg));
        } else if (arg == "--stamp") {
            options.stamp_file = require_next(i, argc, argv, arg);
        } else if (starts_with(arg, "--stamp=")) {
            options.stamp_file = value_after_equals(arg);
        } else if (arg == "--include" || arg == "-I") {
            options.includes.push_back(require_next(i, argc, argv, arg));
        } else if (starts_with(arg, "-I") && arg.size() > 2) {
            options.includes.emplace_back(arg.substr(2));
        } else if (
            arg == "--template" || arg == "-t" || arg == "--threads" ||
            arg == "-j"
        ) {
            (void)require_next(i, argc, argv, arg);
        } else if (
            starts_with(arg, "--template=") || starts_with(arg, "--threads=")
        ) {
            continue;
        } else if (starts_with(arg, "-")) {
            throw std::runtime_error("unknown option: " + std::string(arg));
        } else {
            if (is_header_path(arg)) {
                options.headers.emplace_back(arg);
            }
        }
    }

    if (options.headers.empty() && !options.aggregate) {
        throw std::runtime_error("no headers provided");
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto options = parse_options(argc, argv);

        if (options.aggregate) {
            if (options.output_file.empty()) {
                throw std::runtime_error("aggregate output file is required");
            }
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
