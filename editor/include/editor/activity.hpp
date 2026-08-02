#pragma once

#include "base/types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fei::editor {

enum class OperationSource {
    Editor,
    User,
    ExternalAgent,
};

struct OperationEntry {
    uint64 sequence {0};
    OperationSource source {OperationSource::Editor};
    std::string action;
    std::string detail;
    bool succeeded {true};
};

class ActivityLog {
  public:
    static constexpr std::size_t c_max_entries = 512;

    void record(
        OperationSource source,
        std::string action,
        std::string detail = {},
        bool succeeded = true
    );
    void clear();

    [[nodiscard]] const std::vector<OperationEntry>& entries() const {
        return m_entries;
    }

  private:
    uint64 m_next_sequence {1};
    std::vector<OperationEntry> m_entries;
};

enum class ExternalAgentConnection {
    Disconnected,
    Connected,
};

struct ExternalAgentStatus {
    ExternalAgentConnection connection {ExternalAgentConnection::Disconnected};
    std::string name;
    std::string endpoint;
};

} // namespace fei::editor
