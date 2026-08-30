#pragma once

#include "model.hpp"

#include <filesystem>
#include <string_view>

namespace ets::reflgen {

void write_reflection_manifest(
    const ParseResult& result,
    const std::filesystem::path& root_dir,
    const std::filesystem::path& output_file,
    std::string_view script_module
);

} // namespace ets::reflgen
