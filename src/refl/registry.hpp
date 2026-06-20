#pragma once
#include "refl/enum.hpp"
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <concepts>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace fei {

class Cls;

class Registry {
  public:
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    static Registry& instance();

    Type& register_type(
        TypeId id,
        const std::string& name,
        std::size_t size,
        std::size_t align,
        TypeOps ops
    );
    Type& get_type(TypeId id);
    const Type* try_get_type(TypeId id) const;
    Cls& add_cls(TypeId id);
    Cls& get_cls(TypeId id);
    Enum& add_enum(TypeId id);
    Enum& get_enum(TypeId id);
    bool has_enum(TypeId id) const;
    void clear_generated_metadata();

    template<typename T>
    Cls& register_cls() {
        register_type<T>();
        TypeId id = type_id<T>();
        return add_cls(id);
    }

    template<typename T>
    Cls& get_cls() {
        return get_cls(type_id<T>());
    }

    const auto& clses() const { return m_classes; }
    const auto& enums() const { return m_enums; }

    template<typename T>
    Enum& register_enum() {
        static_assert(std::is_enum_v<T>, "T must be an enum type");
        register_type<T>();
        TypeId id = type_id<T>();
        auto& enm = add_enum(id);
        enm.set_construct_func([](void* dest, std::int64_t underlying_value) {
            new (dest) T(static_cast<T>(underlying_value));
        });
        return enm;
    }

    template<typename T>
    Enum& get_enum() {
        static_assert(std::is_enum_v<T>, "T must be an enum type");
        return get_enum(type_id<T>());
    }

    template<typename T>
    Type& register_type() {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, void> || std::is_function_v<U>) {
            return register_type(
                type_id<U>(),
                std::string(type_name<U>()),
                0,
                0,
                {}
            );
        } else {
            TypeOps ops {};
            if constexpr (std::is_default_constructible_v<U>) {
                ops.default_construct = [](void* dest) {
                    new (dest) U();
                };
            }
            if constexpr (std::is_trivially_copyable_v<U>) {
                ops.copy_construct = [](void* dest, const void* src) {
                    std::memcpy(dest, src, sizeof(U));
                };
            } else if constexpr (std::copy_constructible<U>) {
                ops.copy_construct = [](void* dest, const void* src) {
                    new (dest) U(*static_cast<const U*>(src));
                };
            };
            if constexpr (std::is_trivially_copyable_v<U>) {
                ops.move_construct = [](void* dest, void* src) {
                    std::memcpy(dest, src, sizeof(U));
                };
            } else if constexpr (std::move_constructible<U>) {
                ops.move_construct = [](void* dest, void* src) {
                    new (dest) U(std::move(*static_cast<U*>(src)));
                };
            };
            ops.destroy = [](void* ptr) {
                static_cast<U*>(ptr)->~U();
            };
            if constexpr (std::is_copy_assignable_v<U>) {
                ops.copy_assign = [](void* dest, const void* src) {
                    *static_cast<U*>(dest) = *static_cast<const U*>(src);
                    return true;
                };
            }
            if constexpr (std::is_move_assignable_v<U>) {
                ops.move_assign = [](void* dest, void* src) {
                    *static_cast<U*>(dest) = std::move(*static_cast<U*>(src));
                    return true;
                };
            }
            return register_type(
                type_id<U>(),
                std::string(type_name<U>()),
                sizeof(U),
                alignof(U),
                ops
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
    std::unordered_map<TypeId, Enum> m_enums;
};

Type& type(TypeId id);

template<class T>
Type& type() {
    return type(type_id<T>());
}

const std::string& type_name(TypeId id);

} // namespace fei
