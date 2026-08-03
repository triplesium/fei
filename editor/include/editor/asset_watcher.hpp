#pragma once

#include "asset/path.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei::editor {

enum class AssetFileChangeKind : std::uint8_t {
    Added,
    Modified,
    Removed,
};

struct AssetFileChange {
    AssetFileChangeKind kind;
    AssetPath path;
    bool directory {false};
};

class ProjectAssetWatcher {
  public:
    explicit ProjectAssetWatcher(
        std::filesystem::path root,
        std::chrono::milliseconds poll_interval =
            std::chrono::milliseconds {500},
        std::size_t stable_observations = 2
    );

    [[nodiscard]] Result<std::vector<AssetFileChange>, std::string>
    poll(bool force = false);
    Status<std::string> acknowledge();

  private:
    struct Entry {
        bool directory {false};
        std::uintmax_t size {0};
        std::filesystem::file_time_type write_time;

        bool operator==(const Entry&) const = default;
    };

    struct PendingChange {
        Optional<Entry> observed;
        std::size_t stable_observations {0};
    };

    using Snapshot = std::unordered_map<AssetPath, Entry>;

    [[nodiscard]] Result<Snapshot, std::string> take_snapshot() const;

    std::filesystem::path m_root;
    std::chrono::milliseconds m_poll_interval;
    std::chrono::steady_clock::time_point m_next_poll;
    std::size_t m_required_stable_observations {2};
    bool m_initialized {false};
    Snapshot m_snapshot;
    std::unordered_map<AssetPath, PendingChange> m_pending;
};

} // namespace fei::editor
