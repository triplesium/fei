#include "editor/asset_watcher.hpp"

#include <algorithm>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace fei::editor {

ProjectAssetWatcher::ProjectAssetWatcher(
    std::filesystem::path root,
    std::chrono::milliseconds poll_interval,
    std::size_t stable_observations
) :
    m_root(std::move(root)), m_poll_interval(poll_interval),
    m_required_stable_observations(
        std::max(stable_observations, std::size_t {1})
    ) {}

Result<ProjectAssetWatcher::Snapshot, std::string>
ProjectAssetWatcher::take_snapshot() const {
    Snapshot snapshot;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        m_root,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto& item = *iterator;
        const bool directory = item.is_directory(error);
        if (error) {
            break;
        }
        const bool file = !directory && item.is_regular_file(error);
        if (error) {
            break;
        }
        if (directory || file) {
            const auto relative = item.path().lexically_relative(m_root);
            auto write_time = item.last_write_time(error);
            if (error) {
                break;
            }
            std::uintmax_t size = 0;
            if (file) {
                size = item.file_size(error);
                if (error) {
                    break;
                }
            }
            snapshot.emplace(
                AssetPath(relative.generic_string()).with_source("project"),
                Entry {
                    .directory = directory,
                    .size = size,
                    .write_time = write_time,
                }
            );
        }
        iterator.increment(error);
    }
    if (error) {
        return failure("Failed to scan project assets: " + error.message());
    }
    return snapshot;
}

Status<std::string> ProjectAssetWatcher::acknowledge() {
    auto snapshot = take_snapshot();
    if (!snapshot) {
        return failure(std::move(snapshot.error()));
    }
    m_snapshot = std::move(*snapshot);
    m_pending.clear();
    m_initialized = true;
    m_next_poll = std::chrono::steady_clock::now() + m_poll_interval;
    return {};
}

Result<std::vector<AssetFileChange>, std::string>
ProjectAssetWatcher::poll(bool force) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < m_next_poll) {
        return std::vector<AssetFileChange> {};
    }
    m_next_poll = now + m_poll_interval;

    auto current = take_snapshot();
    if (!current) {
        return failure(std::move(current.error()));
    }
    if (!m_initialized) {
        m_snapshot = std::move(*current);
        m_initialized = true;
        return std::vector<AssetFileChange> {};
    }

    std::unordered_set<AssetPath> paths;
    paths.reserve(m_snapshot.size() + current->size());
    for (const auto& [path, entry] : m_snapshot) {
        (void)entry;
        paths.insert(path);
    }
    for (const auto& [path, entry] : *current) {
        (void)entry;
        paths.insert(path);
    }

    std::vector<AssetFileChange> changes;
    for (const auto& path : paths) {
        Optional<Entry> previous;
        if (const auto entry = m_snapshot.find(path);
            entry != m_snapshot.end()) {
            previous = entry->second;
        }
        Optional<Entry> observed;
        if (const auto entry = current->find(path); entry != current->end()) {
            observed = entry->second;
        }
        if (previous == observed) {
            m_pending.erase(path);
            continue;
        }

        auto& pending = m_pending[path];
        if (pending.observed == observed) {
            ++pending.stable_observations;
        } else {
            pending.observed = observed;
            pending.stable_observations = 1;
        }
        if (pending.stable_observations < m_required_stable_observations) {
            continue;
        }

        const auto kind = !previous ? AssetFileChangeKind::Added :
                          !observed ? AssetFileChangeKind::Removed :
                                      AssetFileChangeKind::Modified;
        changes.push_back(
            AssetFileChange {
                .kind = kind,
                .path = path,
                .directory =
                    observed ? observed->directory : previous->directory,
            }
        );
        if (observed) {
            m_snapshot.insert_or_assign(path, *observed);
        } else {
            m_snapshot.erase(path);
        }
        m_pending.erase(path);
    }
    return changes;
}

} // namespace fei::editor
