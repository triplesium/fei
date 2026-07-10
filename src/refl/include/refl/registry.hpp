#pragma once
#include "base/optional.hpp"
#include "base/result.hpp"
#include "refl/container_adapter.hpp"
// Exposes GenericTypeInfo specializations consumed by register_type<T>().
// NOLINTNEXTLINE(misc-include-cleaner)
#include "refl/containers/standard.hpp"
#include "refl/enum.hpp"
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <array>
#include <concepts>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

class Cls;
struct DynamicStructDesc;
struct DynamicStructLayout;
struct DynamicTypeError;

struct RegistryError {
    enum class Kind {
        TypeNotFound,
        AmbiguousTypeName,
        ClassNotFound,
        EnumNotFound,
        GenericTypeNotFound,
        ContainerAdapterNotFound,
    };

    Kind kind;
    TypeId type_id;
    Optional<std::string> type_name;
    std::string message;

    static RegistryError
    type_not_found(TypeId id, Optional<std::string> name = nullopt) {
        return make(Kind::TypeNotFound, id, std::move(name), "Type");
    }

    static RegistryError ambiguous_type_name(std::string name) {
        auto id = TypeId(std::string_view {name});
        return RegistryError {
            .kind = Kind::AmbiguousTypeName,
            .type_id = id,
            .type_name = name,
            .message = "Type name '" + std::move(name) + "' is ambiguous",
        };
    }

    static RegistryError
    class_not_found(TypeId id, Optional<std::string> name = nullopt) {
        return make(Kind::ClassNotFound, id, std::move(name), "Class");
    }

    static RegistryError
    enum_not_found(TypeId id, Optional<std::string> name = nullopt) {
        return make(Kind::EnumNotFound, id, std::move(name), "Enum");
    }

    static RegistryError
    generic_type_not_found(TypeId id, Optional<std::string> name = nullopt) {
        return make(
            Kind::GenericTypeNotFound,
            id,
            std::move(name),
            "Generic type"
        );
    }

    static RegistryError container_adapter_not_found(
        TypeId id,
        Optional<std::string> name = nullopt
    ) {
        return make(
            Kind::ContainerAdapterNotFound,
            id,
            std::move(name),
            "Container adapter"
        );
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

template<class T>
struct TypeOperationInfo {
    static constexpr bool copy_constructible = std::is_copy_constructible_v<T>;
    static constexpr bool move_constructible =
        std::is_nothrow_move_constructible_v<T>;
    static constexpr bool copy_assignable = std::is_copy_assignable_v<T>;
    static constexpr bool move_assignable =
        std::is_nothrow_move_assignable_v<T>;
};

template<class T>
struct TypeValueOperationInfo {
    static constexpr bool equality_comparable =
        !std::is_array_v<T> && requires(const T& lhs, const T& rhs) {
            { lhs == rhs } -> std::convertible_to<bool>;
        };
    static constexpr bool hashable = requires(const T& value) {
        { std::hash<T> {}(value) } -> std::convertible_to<std::size_t>;
    };
};

template<class T, class Alloc>
struct TypeValueOperationInfo<std::vector<T, Alloc>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<T>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class T, std::size_t Size>
struct TypeValueOperationInfo<std::array<T, Size>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<T>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class First, class Second>
struct TypeValueOperationInfo<std::pair<First, Second>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<First>::equality_comparable &&
        TypeValueOperationInfo<Second>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class... Types>
struct TypeValueOperationInfo<std::tuple<Types...>> {
    static constexpr bool equality_comparable =
        (TypeValueOperationInfo<Types>::equality_comparable && ...);
    static constexpr bool hashable = false;
};

template<class T, class Alloc>
struct TypeOperationInfo<std::vector<T, Alloc>> {
    using Container = std::vector<T, Alloc>;

    static constexpr bool copy_constructible = std::is_copy_constructible_v<T>;
    static constexpr bool move_constructible =
        std::is_nothrow_move_constructible_v<Container>;
    static constexpr bool copy_assignable =
        std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>;
    static constexpr bool move_assignable =
        std::is_nothrow_move_assignable_v<Container>;
};

template<class Key, class Mapped, class Hash, class Eq, class Alloc>
struct TypeValueOperationInfo<
    std::unordered_map<Key, Mapped, Hash, Eq, Alloc>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<Key>::equality_comparable &&
        TypeValueOperationInfo<Mapped>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class Key, class Mapped, class Hash, class Eq, class Alloc>
struct TypeOperationInfo<std::unordered_map<Key, Mapped, Hash, Eq, Alloc>> {
    using Container = std::unordered_map<Key, Mapped, Hash, Eq, Alloc>;

    static constexpr bool copy_constructible =
        std::is_copy_constructible_v<Key> &&
        std::is_copy_constructible_v<Mapped> &&
        std::is_copy_constructible_v<Hash> && std::is_copy_constructible_v<Eq>;
    static constexpr bool move_constructible =
        std::is_nothrow_move_constructible_v<Container>;
    static constexpr bool copy_assignable = copy_constructible &&
                                            std::is_copy_assignable_v<Hash> &&
                                            std::is_copy_assignable_v<Eq>;
    static constexpr bool move_assignable =
        std::is_nothrow_move_assignable_v<Container>;
};

template<class Key, class Mapped, class Compare, class Alloc>
struct TypeValueOperationInfo<std::map<Key, Mapped, Compare, Alloc>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<Key>::equality_comparable &&
        TypeValueOperationInfo<Mapped>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class Key, class Mapped, class Compare, class Alloc>
struct TypeOperationInfo<std::map<Key, Mapped, Compare, Alloc>> {
    using Container = std::map<Key, Mapped, Compare, Alloc>;

    static constexpr bool copy_constructible =
        std::is_copy_constructible_v<Key> &&
        std::is_copy_constructible_v<Mapped> &&
        std::is_copy_constructible_v<Compare>;
    static constexpr bool move_constructible =
        std::is_nothrow_move_constructible_v<Container>;
    static constexpr bool copy_assignable =
        copy_constructible && std::is_copy_assignable_v<Compare>;
    static constexpr bool move_assignable =
        std::is_nothrow_move_assignable_v<Container>;
};

template<class Key, class Compare, class Alloc>
struct TypeValueOperationInfo<std::set<Key, Compare, Alloc>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<Key>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class Key, class Compare, class Alloc>
struct TypeOperationInfo<std::set<Key, Compare, Alloc>> {
    using Container = std::set<Key, Compare, Alloc>;

    static constexpr bool copy_constructible =
        std::is_copy_constructible_v<Key> &&
        std::is_copy_constructible_v<Compare>;
    static constexpr bool move_constructible =
        std::is_nothrow_move_constructible_v<Container>;
    static constexpr bool copy_assignable =
        copy_constructible && std::is_copy_assignable_v<Compare>;
    static constexpr bool move_assignable =
        std::is_nothrow_move_assignable_v<Container>;
};

template<class Key, class Hash, class Eq, class Alloc>
struct TypeValueOperationInfo<std::unordered_set<Key, Hash, Eq, Alloc>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<Key>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class Key, class Hash, class Eq, class Alloc>
struct TypeOperationInfo<std::unordered_set<Key, Hash, Eq, Alloc>> {
    using Container = std::unordered_set<Key, Hash, Eq, Alloc>;

    static constexpr bool copy_constructible =
        std::is_copy_constructible_v<Key> &&
        std::is_copy_constructible_v<Hash> && std::is_copy_constructible_v<Eq>;
    static constexpr bool move_constructible =
        std::is_nothrow_move_constructible_v<Container>;
    static constexpr bool copy_assignable = copy_constructible &&
                                            std::is_copy_assignable_v<Hash> &&
                                            std::is_copy_assignable_v<Eq>;
    static constexpr bool move_assignable =
        std::is_nothrow_move_assignable_v<Container>;
};

template<class T>
struct TypeValueOperationInfo<Optional<T>> {
    static constexpr bool equality_comparable =
        TypeValueOperationInfo<T>::equality_comparable;
    static constexpr bool hashable = false;
};

template<class T, class E>
struct TypeValueOperationInfo<Result<T, E>> {
    // Result wraps std::expected and does not currently expose an unambiguous
    // value-level equality operation on every supported compiler.
    static constexpr bool equality_comparable = false;
    static constexpr bool hashable = false;
};

template<class T>
struct TypeOperationInfo<Optional<T>> {
    using Container = Optional<T>;

    static constexpr bool copy_constructible = std::is_copy_constructible_v<T>;
    static constexpr bool move_constructible =
        std::is_nothrow_move_constructible_v<Container>;
    static constexpr bool copy_assignable =
        std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>;
    static constexpr bool move_assignable =
        std::is_nothrow_move_assignable_v<Container>;
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
    Result<Type&, RegistryError> try_get_type_exact(std::string_view name);
    Result<Type&, RegistryError> try_get_type(std::string_view name);
    Result<Type&, RegistryError> try_get_type(const std::string& name) {
        return try_get_type(std::string_view {name});
    }
    Result<Type&, RegistryError> try_get_type(const char* name) {
        return try_get_type(std::string_view {name});
    }
    Cls& add_cls(TypeId id);
    Cls& get_cls(TypeId id);
    Result<Cls&, RegistryError> try_get_cls(TypeId id);
    Enum& add_enum(TypeId id);
    Enum& get_enum(TypeId id);
    Result<Enum&, RegistryError> try_get_enum(TypeId id);
    GenericType& register_generic_type(TypeId id, GenericType generic_type);
    GenericType& get_generic_type(TypeId id);
    Result<GenericType&, RegistryError> try_get_generic_type(TypeId id);
    ContainerAdapter& register_container_adapter(
        TypeId id,
        std::unique_ptr<ContainerAdapter> adapter
    );
    ContainerAdapter& get_container_adapter(TypeId id);
    Result<ContainerAdapter&, RegistryError>
    try_get_container_adapter(TypeId id);
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
    const auto& generic_types() const { return m_generic_types; }
    const auto& container_adapters() const { return m_container_adapters; }

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
    GenericType& get_generic_type() {
        return get_generic_type(type_id<T>());
    }

    template<typename T>
    Result<GenericType&, RegistryError> try_get_generic_type() {
        auto result = try_get_generic_type(type_id<T>());
        if (!result) {
            return failure(
                RegistryError::generic_type_not_found(
                    type_id<T>(),
                    std::string(type_name<T>())
                )
            );
        }
        return *result;
    }

    template<typename T>
    ContainerAdapter& get_container_adapter() {
        return get_container_adapter(type_id<T>());
    }

    template<typename T>
    Result<ContainerAdapter&, RegistryError> try_get_container_adapter() {
        auto result = try_get_container_adapter(type_id<T>());
        if (!result) {
            return failure(
                RegistryError::container_adapter_not_found(
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
        Type* registered = nullptr;
        if constexpr (std::is_same_v<U, void> || std::is_function_v<U>) {
            registered = &register_type(
                type_id<U>(),
                std::string(type_name<U>()),
                0,
                0,
                {}
            );
        } else {
            static_assert(
                std::is_nothrow_destructible_v<U>,
                "Reflected types must have a noexcept destructor"
            );
            TypeOps ops {};
            using OpsInfo = TypeOperationInfo<U>;
            using ValueOpsInfo = TypeValueOperationInfo<U>;
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
            } else if constexpr (OpsInfo::copy_constructible) {
                ops.copy_construct =
                    [](const void*, void* dest, const void* src) {
                        new (dest) U(*static_cast<const U*>(src));
                    };
            };
            if constexpr (std::is_trivially_copyable_v<U>) {
                ops.move_construct =
                    [](const void*, void* dest, void* src) noexcept {
                        std::memcpy(dest, src, sizeof(U));
                    };
            } else if constexpr (OpsInfo::move_constructible) {
                ops.move_construct =
                    [](const void*, void* dest, void* src) noexcept {
                        new (dest) U(std::move(*static_cast<U*>(src)));
                    };
            };
            ops.destroy = [](const void*, void* ptr) noexcept {
                static_cast<U*>(ptr)->~U();
            };
            if constexpr (OpsInfo::copy_assignable) {
                ops.copy_assign = [](const void*, void* dest, const void* src) {
                    *static_cast<U*>(dest) = *static_cast<const U*>(src);
                    return true;
                };
            }
            if constexpr (OpsInfo::move_assignable) {
                ops.move_assign = [](const void*,
                                     void* dest,
                                     void* src) noexcept {
                    *static_cast<U*>(dest) = std::move(*static_cast<U*>(src));
                    return true;
                };
            }
            if constexpr (ValueOpsInfo::equality_comparable) {
                ops.equal =
                    [](const void*, const void* lhs, const void* rhs) -> bool {
                    return static_cast<bool>(
                        *static_cast<const U*>(lhs) ==
                        *static_cast<const U*>(rhs)
                    );
                };
            }
            if constexpr (ValueOpsInfo::hashable) {
                ops.hash_value = [](const void*,
                                    const void* value) -> std::size_t {
                    return static_cast<std::size_t>(
                        std::hash<U> {}(*static_cast<const U*>(value))
                    );
                };
            }
            registered = &register_type(
                type_id<U>(),
                std::string(type_name<U>()),
                sizeof(U),
                alignof(U),
                ops
            );
        }
        register_generic_type_for<U>();
        return *registered;
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

    template<class... Dependencies>
    void register_generic_type_dependencies(std::tuple<Dependencies...>*) {
        (register_type<Dependencies>(), ...);
    }

    template<class T>
    void register_generic_type_for() {
        using U = std::remove_cvref_t<T>;
        if constexpr (GenericTypeInfo<U>::supported) {
            auto id = type_id<U>();
            if (m_generic_types.contains(id)) {
                return;
            }
            register_generic_type_dependencies(
                static_cast<typename GenericTypeInfo<U>::Dependencies*>(nullptr)
            );

            if constexpr (requires {
                              GenericTypeInfo<U>::make_container_adapter();
                          }) {
                auto adapter = GenericTypeInfo<U>::make_container_adapter();
                if (adapter) {
                    register_container_adapter(id, std::move(adapter));
                }
            }

            auto argument_type_ids = GenericTypeInfo<U>::argument_type_ids();
            auto arguments = [&] {
                if constexpr (requires { GenericTypeInfo<U>::arguments(); }) {
                    return GenericTypeInfo<U>::arguments();
                } else {
                    return generic_arguments_from_type_ids(argument_type_ids);
                }
            }();

            register_generic_type(
                id,
                GenericType {
                    .specialized_type_id = id,
                    .generic_type_id = GenericTypeInfo<U>::generic_type_id(),
                    .generic_name = GenericTypeInfo<U>::generic_name(),
                    .argument_type_ids = std::move(argument_type_ids),
                    .arguments = std::move(arguments),
                }
            );
        }
    }

    std::unordered_map<TypeId, Type> m_types;
    std::unordered_map<std::string, TypeId> m_type_ids_by_name;
    std::unordered_map<TypeId, Cls> m_classes;
    std::unordered_map<TypeId, Enum> m_enums;
    std::unordered_map<TypeId, GenericType> m_generic_types;
    std::unordered_map<TypeId, std::unique_ptr<ContainerAdapter>>
        m_container_adapters;
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
