#include "editor/activity.hpp"

#include <utility>

namespace fei::editor {

void ActivityLog::record(
    OperationSource source,
    std::string action,
    std::string detail,
    bool succeeded
) {
    if (m_entries.size() == c_max_entries) {
        m_entries.erase(m_entries.begin());
    }
    m_entries.push_back(
        OperationEntry {
            .sequence = m_next_sequence++,
            .source = source,
            .action = std::move(action),
            .detail = std::move(detail),
            .succeeded = succeeded,
        }
    );
}

void ActivityLog::clear() {
    m_entries.clear();
}

} // namespace fei::editor
