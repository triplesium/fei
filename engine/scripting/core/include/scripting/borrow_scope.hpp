#pragma once

#include <cstdint>

namespace ets {

struct ScriptBorrowToken {
    std::uint64_t epoch {0};
};

class ScriptBorrowScope {
  private:
    std::uint64_t m_epoch {0};
    bool m_active {false};

  public:
    ScriptBorrowToken begin() {
        ++m_epoch;
        m_active = true;
        return {.epoch = m_epoch};
    }

    void end(ScriptBorrowToken token) {
        if (token.epoch == m_epoch) {
            m_active = false;
        }
    }

    bool valid(ScriptBorrowToken token) const {
        return m_active && token.epoch == m_epoch;
    }
};

} // namespace ets
