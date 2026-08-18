#pragma once

#include "base/result.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fei::agentd {

struct ArtifactMetadata {
    std::string id;
    std::string content_type;
    std::size_t size {0};
};

struct StoredArtifact {
    ArtifactMetadata metadata;
    std::vector<std::byte> data;
};

class ArtifactStore {
  public:
    explicit ArtifactStore(
        std::size_t maximum_count = 16,
        std::size_t maximum_bytes = std::size_t {64} * 1024 * 1024
    );

    [[nodiscard]] Result<ArtifactMetadata, std::string>
    store(std::string content_type, std::vector<std::byte> data);
    [[nodiscard]] std::optional<StoredArtifact> find(std::string_view id) const;

  private:
    void evict_oldest();

    std::size_t m_maximum_count;
    std::size_t m_maximum_bytes;
    std::size_t m_stored_bytes {0};
    std::uint64_t m_next_id {1};
    mutable std::mutex m_mutex;
    std::deque<std::string> m_order;
    std::unordered_map<std::string, StoredArtifact> m_artifacts;
};

} // namespace fei::agentd
