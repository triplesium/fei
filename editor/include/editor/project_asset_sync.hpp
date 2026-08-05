#pragma once

#include "base/optional.hpp"

#include <string>

namespace fei {

class World;

namespace editor {

class ProjectAssetSynchronizer {
  public:
    void update(World& world);

    [[nodiscard]] const Optional<std::string>& last_error() const noexcept {
        return m_last_error;
    }

  private:
    Optional<std::string> m_last_error;
};

} // namespace editor
} // namespace fei
