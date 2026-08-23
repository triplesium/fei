#include "artifact_store.hpp"

#include <utility>

namespace ets::agentd {

ArtifactStore::ArtifactStore(
    std::size_t maximum_count,
    std::size_t maximum_bytes
) : m_maximum_count(maximum_count), m_maximum_bytes(maximum_bytes) {}

Result<ArtifactMetadata, std::string>
ArtifactStore::store(std::string content_type, std::vector<std::byte> data) {
    if (content_type.empty()) {
        return failure(std::string("Artifact content type must not be empty"));
    }
    if (data.empty()) {
        return failure(std::string("Artifact data must not be empty"));
    }
    if (m_maximum_count == 0 || data.size() > m_maximum_bytes) {
        return failure(std::string("Artifact exceeds the store capacity"));
    }

    std::scoped_lock lock(m_mutex);
    while (!m_order.empty() &&
           (m_artifacts.size() >= m_maximum_count ||
            m_stored_bytes > m_maximum_bytes - data.size())) {
        evict_oldest();
    }

    ArtifactMetadata metadata {
        .id = "capture-" + std::to_string(m_next_id++),
        .content_type = std::move(content_type),
        .size = data.size(),
    };
    StoredArtifact artifact {
        .metadata = metadata,
        .data = std::move(data),
    };
    m_stored_bytes += artifact.data.size();
    m_order.push_back(metadata.id);
    m_artifacts.emplace(metadata.id, std::move(artifact));
    return metadata;
}

std::optional<StoredArtifact> ArtifactStore::find(std::string_view id) const {
    std::scoped_lock lock(m_mutex);
    const auto artifact = m_artifacts.find(std::string(id));
    if (artifact == m_artifacts.end()) {
        return std::nullopt;
    }
    return artifact->second;
}

void ArtifactStore::evict_oldest() {
    const auto id = std::move(m_order.front());
    m_order.pop_front();
    const auto artifact = m_artifacts.find(id);
    if (artifact == m_artifacts.end()) {
        return;
    }
    m_stored_bytes -= artifact->second.data.size();
    m_artifacts.erase(artifact);
}

} // namespace ets::agentd
