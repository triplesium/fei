#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ets::reflgen {

void write_reflection_metadata(
    const ParseResult& result,
    const std::filesystem::path& output_file,
    const std::string& script_module
);

void validate_reflection_metadata(
    const std::vector<std::string>& metadata_files
);

} // namespace ets::reflgen
