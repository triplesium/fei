#pragma once

#include <cstdint>

namespace ets {

struct LuauBorrowToken {
    std::uint64_t epoch {0};
};

class LuauBorrowScope {
  private:
    std::uint64_t m_epoch {0};
    bool m_active {false};

  public:
    LuauBorrowToken begin() {
        ++m_epoch;
        m_active = true;
        return {.epoch = m_epoch};
    }

    void end(LuauBorrowToken token) {
        if (token.epoch == m_epoch) {
            m_active = false;
        }
    }

    bool valid(LuauBorrowToken token) const {
        return m_active && token.epoch == m_epoch;
    }
};

} // namespace ets
