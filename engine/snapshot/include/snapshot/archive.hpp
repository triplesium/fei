#pragma once

#include "base/result.hpp"
#include "serialization/node.hpp"
#include "snapshot/world_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ets::snapshot {

struct SnapshotArchiveMetadata {
    std::string project;
    std::string engine_build;
    std::string runtime_signature;
    std::string script_hash;

    bool operator==(const SnapshotArchiveMetadata&) const = default;
};

struct SnapshotArchive {
    static constexpr std::uint32_t c_current_version = 1;

    std::uint32_t version {c_current_version};
    SnapshotArchiveMetadata metadata;
    WorldSnapshot snapshot;
};

struct SnapshotArchiveLimits {
    std::size_t max_file_bytes {64 * 1024 * 1024};
};

Result<std::string, SnapshotError>
runtime_compatibility_signature(const World& world);

Result<serialization::SerializedNode, SnapshotError>
encode_archive(const SnapshotArchive& archive);

Result<SnapshotArchive, SnapshotError>
decode_archive(const serialization::SerializedNode& node);

Status<SnapshotError> validate_archive_metadata(
    const SnapshotArchiveMetadata& actual,
    const SnapshotArchiveMetadata& expected
);

Status<SnapshotError> save_archive_file(
    const std::filesystem::path& path,
    const SnapshotArchive& archive
);

Result<SnapshotArchive, SnapshotError> load_archive_file(
    const std::filesystem::path& path,
    const SnapshotArchiveMetadata& expected,
    SnapshotArchiveLimits limits = {}
);

} // namespace ets::snapshot
