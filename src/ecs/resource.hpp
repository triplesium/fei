#pragma once

#include "refl/val.hpp"

#include <unordered_map>

namespace fei {

class Resources {
  private:
    std::unordered_map<TypeId, Val> m_resources;

  public:
    Resources() = default;

    void set(TypeId type_id, Val val) {
        m_resources.emplace(type_id, std::move(val));
    }

    Ref get(TypeId type_id) const {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            return it->second.ref();
        }
        return {};
    }
};

} // namespace fei
