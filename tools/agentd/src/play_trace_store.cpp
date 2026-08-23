#include "play_trace_store.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ets::agentd {
namespace {

int64 unix_time_milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

} // namespace

PlayTraceStore::PlayTraceStore(PlayTraceLimits limits) : m_limits(limits) {}

Result<uint64, std::string> PlayTraceStore::append(nlohmann::json event) {
    if (!event.is_object()) {
        return failure(std::string("Play trace event must be an object"));
    }
    if (m_limits.maximum_events == 0 || m_limits.maximum_bytes == 0 ||
        m_limits.maximum_event_bytes == 0) {
        return failure(std::string("Play trace store limits must be positive"));
    }

    std::scoped_lock lock(m_mutex);
    const auto sequence = m_next_sequence++;
    event["sequence"] = sequence;
    event["timestamp_ms"] = unix_time_milliseconds();

    auto encoded_size = event.dump().size();
    if (encoded_size > m_limits.maximum_event_bytes ||
        encoded_size > m_limits.maximum_bytes) {
        nlohmann::json summary {
            {"sequence", sequence},
            {"timestamp_ms", event.at("timestamp_ms")},
            {"type", event.value("type", "event")},
            {"name", event.value("name", "unknown")},
            {"data",
             {
                 {"truncated", true},
                 {"original_bytes", encoded_size},
             }},
        };
        for (const auto* key : {"trace_id", "span_id", "parent_span_id"}) {
            if (event.contains(key)) {
                summary[key] = event.at(key);
            }
        }
        event = std::move(summary);
        encoded_size = event.dump().size();
    }

    m_events.push_back(
        StoredEvent {
            .sequence = sequence,
            .bytes = encoded_size,
            .value = std::move(event),
        }
    );
    m_bytes += encoded_size;
    evict_to_bounds();
    m_available.notify_all();
    return sequence;
}

PlayTraceBatch
PlayTraceStore::read_after(uint64 sequence, std::size_t limit) const {
    std::scoped_lock lock(m_mutex);
    return read_after_locked(sequence, limit);
}

PlayTraceBatch PlayTraceStore::wait_after(
    uint64 sequence,
    std::size_t limit,
    std::chrono::milliseconds timeout
) const {
    std::unique_lock lock(m_mutex);
    m_available.wait_for(lock, timeout, [this, sequence]() {
        if (m_events.empty()) {
            return false;
        }
        return m_events.back().sequence > sequence ||
               sequence < m_events.front().sequence - 1;
    });
    return read_after_locked(sequence, limit);
}

PlayTraceBatch
PlayTraceStore::read_after_locked(uint64 sequence, std::size_t limit) const {
    PlayTraceBatch batch;
    batch.oldest =
        m_events.empty() ? m_next_sequence : m_events.front().sequence;
    batch.latest =
        m_events.empty() ? m_next_sequence - 1 : m_events.back().sequence;
    batch.gap = sequence != 0 && sequence < batch.oldest - 1;
    batch.events.reserve(std::min(limit, m_events.size()));
    for (const auto& event : m_events) {
        if (event.sequence <= sequence) {
            continue;
        }
        if (batch.events.size() >= limit) {
            break;
        }
        batch.events.push_back(event.value);
    }
    return batch;
}

void PlayTraceStore::evict_to_bounds() {
    while (!m_events.empty() && (m_events.size() > m_limits.maximum_events ||
                                 m_bytes > m_limits.maximum_bytes)) {
        m_bytes -= m_events.front().bytes;
        m_events.pop_front();
    }
}

} // namespace ets::agentd
