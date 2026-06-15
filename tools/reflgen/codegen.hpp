#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fei::reflgen {

void generate_cpp_file(
    const ParseResult& result,
    const std::filesystem::path& root_dir,
    const std::filesystem::path& output_file,
    const std::string& function_name
);

void generate_aggregate_cpp_file(
    const std::vector<std::string>& function_names,
    const std::filesystem::path& output_file
);

} // namespace fei::reflgen
