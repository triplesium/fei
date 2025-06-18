#pragma once
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace fei {

class Registry {
  public:
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    static Registry& instance();

    template<typename T>
    Type& register_type() {
        auto id = type_id<T>();
        if (m_types.contains(id)) {
            return m_types.at(id);
        }
        Type type(std::string(type_name<T>()), id, sizeof(T));
        m_types.emplace(id, type);
        return m_types.at(id);
    }

    template<typename T>
    Type& get_type() {
        auto id = type_id<T>();
        if (m_types.contains(id)) {
            return m_types.at(id);
        }
        return register_type<T>();
    }

    Type& get_type(TypeId id) {
        auto it = m_types.find(id);
        if (it != m_types.end()) {
            return it->second;
        }
        throw std::runtime_error("Type not found");
    }

  private:
    Registry() = default;
    static Registry* s_instance;

    std::unordered_map<TypeId, Type> m_types;
    TypeId m_next_id = 1;
};

template<class T>
Type& type() {
    return Registry::instance().get_type<T>();
}

} // namespace fei
