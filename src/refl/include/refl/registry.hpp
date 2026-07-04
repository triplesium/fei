#pragma once
#include "base/optional.hpp"
#include "base/result.hpp"
#include "refl/enum.hpp"
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <concepts>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace fei {

class Cls;
struct DynamicStructDesc;
struct DynamicStructLayout;
struct DynamicTypeError;

struct RegistryError {
    enum class Kind { TypeNotFound, ClassNotFound, EnumNotFound };

    Kind kind;
    TypeId type_id;
    Optional<std::string> type_name;
    std::string message;

    static RegistryError
    type_not_found(TypeId id, Optional<std::string> name = nullopt) {
        return make(Kind::TypeNotFound, id, std::move(name), "Type");
    }

    static RegistryError
    class_not_found(TypeId id, Optional<std::string> name = nullopt) {
        return make(Kind::ClassNotFound, id, std::move(name), "Class");
    }

    static RegistryError
    enum_not_found(TypeId id, Optional<std::string> name = nullopt) {
        return make(Kind::EnumNotFound, id, std::move(name), "Enum");
    }

  private:
    static RegistryError
    make(Kind kind, TypeId id, Optional<std::string> name, const char* label) {
        std::string target =
            name ? "'" + *name + "'" : "id " + std::to_string(id.id());
        return RegistryError {
            .kind = kind,
            .type_id = id,
            .type_name = std::move(name),
            .message = std::string(label) + " not found for " + target,
        };
    }
};

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
    Result<Type&, RegistryError> try_get_type(TypeId id);
    Result<Type&, RegistryError> try_get_type(std::string_view name);
    Cls& add_cls(TypeId id);
    Cls& get_cls(TypeId id);
    Result<Cls&, RegistryError> try_get_cls(TypeId id);
    Enum& add_enum(TypeId id);
    Enum& get_enum(TypeId id);
    Result<Enum&, RegistryError> try_get_enum(TypeId id);
    bool has_enum(TypeId id) const;
    void clear_generated_metadata();

    Result<Type&, DynamicTypeError>
    register_dynamic_struct(DynamicStructDesc desc);
    const DynamicStructLayout* try_get_dynamic_struct_layout(TypeId id) const;

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

    template<typename T>
    Result<Cls&, RegistryError> try_get_cls() {
        auto result = try_get_cls(type_id<T>());
        if (!result) {
            return failure(
                RegistryError::class_not_found(
                    type_id<T>(),
                    std::string(type_name<T>())
                )
            );
        }
        return *result;
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
    Result<Enum&, RegistryError> try_get_enum() {
        static_assert(std::is_enum_v<T>, "T must be an enum type");
        auto result = try_get_enum(type_id<T>());
        if (!result) {
            return failure(
                RegistryError::enum_not_found(
                    type_id<T>(),
                    std::string(type_name<T>())
                )
            );
        }
        return *result;
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
                ops.default_construct = [](const void*, void* dest) {
                    new (dest) U();
                };
            }
            if constexpr (std::is_trivially_copyable_v<U>) {
                ops.copy_construct =
                    [](const void*, void* dest, const void* src) {
                        std::memcpy(dest, src, sizeof(U));
                    };
            } else if constexpr (std::copy_constructible<U>) {
                ops.copy_construct =
                    [](const void*, void* dest, const void* src) {
                        new (dest) U(*static_cast<const U*>(src));
                    };
            };
            if constexpr (std::is_trivially_copyable_v<U>) {
                ops.move_construct = [](const void*, void* dest, void* src) {
                    std::memcpy(dest, src, sizeof(U));
                };
            } else if constexpr (std::move_constructible<U>) {
                ops.move_construct = [](const void*, void* dest, void* src) {
                    new (dest) U(std::move(*static_cast<U*>(src)));
                };
            };
            ops.destroy = [](const void*, void* ptr) {
                static_cast<U*>(ptr)->~U();
            };
            if constexpr (std::is_copy_assignable_v<U>) {
                ops.copy_assign = [](const void*, void* dest, const void* src) {
                    *static_cast<U*>(dest) = *static_cast<const U*>(src);
                    return true;
                };
            }
            if constexpr (std::is_move_assignable_v<U>) {
                ops.move_assign = [](const void*, void* dest, void* src) {
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

    template<typename T>
    Result<Type&, RegistryError> try_get_type() {
        auto result = try_get_type(type_id<T>());
        if (!result) {
            return failure(
                RegistryError::type_not_found(
                    type_id<T>(),
                    std::string(type_name<T>())
                )
            );
        }
        return *result;
    }

  private:
    Registry() = default;
    static Registry* s_instance;

    Optional<std::string> registered_type_name(TypeId id) const;

    std::unordered_map<TypeId, Type> m_types;
    std::unordered_map<TypeId, Cls> m_classes;
    std::unordered_map<TypeId, Enum> m_enums;
    std::unordered_map<TypeId, std::shared_ptr<const DynamicStructLayout>>
        m_dynamic_structs;
};

Type& type(TypeId id);

template<class T>
Type& type() {
    return type(type_id<T>());
}

const std::string& type_name(TypeId id);

} // namespace fei
