#pragma once
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <string>
#include <type_traits>
#include <unordered_map>

namespace fei {

class Cls;

class Registry {
  public:
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    static Registry& instance();

    Type& register_type(TypeId id, const std::string& name, std::size_t size);
    Type& get_type(TypeId id);
    Cls& add_cls(TypeId id);
    Cls& get_cls(TypeId id);

    template<typename T>
    Cls& register_cls() {
        TypeId id = type_id<T>();
        return add_cls(id);
    }

    template<typename T>
    Cls& get_cls() {
        return get_cls(type_id<T>());
    }

    template<typename T>
    Type& register_type() {
        if constexpr (std::is_same_v<std::decay_t<T>, void>) {
            return register_type(type_id<T>(), std::string(type_name<T>()), 0);
        } else {
            return register_type(
                type_id<T>(),
                std::string(type_name<T>()),
                sizeof(T)
            );
        }
    }

    template<typename T>
    Type& get_type() {
        return get_type(type_id<T>());
    }

  private:
    Registry() = default;
    static Registry* s_instance;

    std::unordered_map<TypeId, Type> m_types;
    std::unordered_map<TypeId, Cls> m_classes;
    TypeId m_next_id = 1;
};

Type& type(TypeId id);

template<class T>
Type& type() {
    return type(type_id<T>());
}

const std::string& type_name(TypeId id);

} // namespace fei
