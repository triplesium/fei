#pragma once

#include "base/result.hpp"
#include "snapshot/archive.hpp"

#include <string>

namespace fei {

class Project;

namespace runtime_host {

inline constexpr const char* c_default_snapshot_engine_build =
    "fei-runtime-host/archive-v1";

Result<std::string, std::string> current_runtime_build_id();

Result<snapshot::SnapshotArchiveMetadata, std::string>
make_snapshot_archive_metadata(
    const Project& project,
    std::string engine_build = c_default_snapshot_engine_build
);

} // namespace runtime_host
} // namespace fei
