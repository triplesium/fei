#pragma once

#include "model.hpp"

#include <filesystem>
#include <span>

namespace ets::luau_defgen {

[[nodiscard]] Database
load_manifests(std::span<const std::filesystem::path> manifest_files);

} // namespace ets::luau_defgen
