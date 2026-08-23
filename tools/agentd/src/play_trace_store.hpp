#pragma once

#include "base/result.hpp"
#include "base/types.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <string>
#include <vector>

namespace ets::agentd {

struct PlayTraceLimits {
    std::size_t maximum_events {2000};
    std::size_t maximum_bytes {std::size_t {16} * 1024 * 1024};
    std::size_t maximum_event_bytes {std::size_t {512} * 1024};
};

struct PlayTraceBatch {
    uint64 oldest {};
    uint64 latest {};
    bool gap {false};
    std::vector<nlohmann::json> events;
};

class PlayTraceStore {
  public:
    explicit PlayTraceStore(PlayTraceLimits limits = {});

    [[nodiscard]] Result<uint64, std::string> append(nlohmann::json event);
    [[nodiscard]] PlayTraceBatch
    read_after(uint64 sequence, std::size_t limit) const;
    [[nodiscard]] PlayTraceBatch wait_after(
        uint64 sequence,
        std::size_t limit,
        std::chrono::milliseconds timeout
    ) const;

  private:
    struct StoredEvent {
        uint64 sequence {};
        std::size_t bytes {};
        nlohmann::json value;
    };

    [[nodiscard]] PlayTraceBatch
    read_after_locked(uint64 sequence, std::size_t limit) const;
    void evict_to_bounds();

    PlayTraceLimits m_limits;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_available;
    std::deque<StoredEvent> m_events;
    std::size_t m_bytes {};
    uint64 m_next_sequence {1};
};

} // namespace ets::agentd
