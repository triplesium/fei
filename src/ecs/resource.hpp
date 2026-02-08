#pragma once

#include "refl/type.hpp"
#include "refl/val.hpp"

#include <unordered_map>

namespace fei {

class Resources {
  private:
    std::unordered_map<TypeId, Val> m_resources;

  public:
    Resources() = default;

    bool contains(TypeId type_id) const {
        return m_resources.contains(type_id);
    }

    template<typename T>
    void set(TypeId type_id, T&& val) {
        m_resources.insert_or_assign(
            type_id,
            make_val<T>(std::forward<T>(val))
        );
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
