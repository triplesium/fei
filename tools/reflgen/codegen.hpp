#pragma once

#include "model.hpp"

#include <filesystem>

namespace fei::reflgen {

void generate_cpp_file(
    const ParseResult& result,
    const std::filesystem::path& root_dir,
    const std::filesystem::path& output_file
);

} // namespace fei::reflgen
