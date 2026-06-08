#pragma once

#include "refl/utils.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace fei {

constexpr std::uint64_t stable_type_hash(std::string_view name) {
    std::uint64_t hash = 14695981039346656037ull;
    for (char c : name) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ull;
    }
    hash &= 0x7fffffffffffffffull;
    return hash == 0 ? 1 : hash;
}

class TypeId {
  private:
    std::uint64_t m_id;

  public:
    TypeId() : m_id(0) {}
    TypeId(std::uint64_t id) : m_id(id) {}
    TypeId(std::string_view name) : m_id(stable_type_hash(name)) {}
    TypeId(const std::string& name) : TypeId(std::string_view {name}) {}

    std::uint64_t id() const { return m_id; }

    auto operator<=>(const TypeId& other) const { return m_id <=> other.m_id; }
    bool operator==(const TypeId& other) const { return m_id == other.m_id; }

    operator std::uint64_t() const { return m_id; }
    operator bool() const { return m_id != 0; }
};

template<typename T>
inline TypeId type_id() {
    return TypeId(type_name<std::remove_cvref_t<T>>());
}

struct TypeOps {
    using DefaultConstructFunc = void (*)(void* dest);
    using CopyConstructFunc = void (*)(void* dest, const void* src);
    using MoveConstructFunc = void (*)(void* dest, void* src);
    using DestroyFunc = void (*)(void* ptr);
    using CopyAssignFunc = bool (*)(void* dest, const void* src);
    using MoveAssignFunc = bool (*)(void* dest, void* src);

    DefaultConstructFunc default_construct {nullptr};
    CopyConstructFunc copy_construct {nullptr};
    MoveConstructFunc move_construct {nullptr};
    DestroyFunc destroy {nullptr};
    CopyAssignFunc copy_assign {nullptr};
    MoveAssignFunc move_assign {nullptr};
};

class Type {
  public:
    using DefaultConstructFunc = TypeOps::DefaultConstructFunc;
    using CopyConstructFunc = TypeOps::CopyConstructFunc;
    using MoveConstructFunc = TypeOps::MoveConstructFunc;
    using DeleteFunc = TypeOps::DestroyFunc;
    using DestroyFunc = TypeOps::DestroyFunc;
    using CopyAssignFunc = TypeOps::CopyAssignFunc;
    using MoveAssignFunc = TypeOps::MoveAssignFunc;

  private:
    std::string m_name;
    TypeId m_id;
    std::size_t m_size;
    std::size_t m_align;
    TypeOps m_ops;

  public:
    Type(
        std::string name,
        TypeId id,
        std::size_t size,
        std::size_t align,
        TypeOps ops
    ) :
        m_name(std::move(name)), m_id(id), m_size(size), m_align(align),
        m_ops(ops) {}

    const std::string& name() const { return m_name; }
    TypeId hash() const { return m_id; }
    TypeId id() const { return m_id; }
    std::size_t size() const { return m_size; }
    std::size_t align() const { return m_align; }
    bool is_number() const;
    bool is_integral() const;
    bool is_floating_point() const;
    std::string stripped_name() const;
    const TypeOps& ops() const { return m_ops; }
    DefaultConstructFunc default_construct_func() const {
        return m_ops.default_construct;
    }
    CopyConstructFunc copy_construct_func() const { return m_ops.copy_construct; }
    MoveConstructFunc move_construct_func() const { return m_ops.move_construct; }
    DeleteFunc delete_func() const { return m_ops.destroy; }
    DestroyFunc destroy_func() const { return m_ops.destroy; }
    CopyAssignFunc copy_assign_func() const { return m_ops.copy_assign; }
    MoveAssignFunc move_assign_func() const { return m_ops.move_assign; }

    bool default_constructible() const {
        return m_ops.default_construct != nullptr;
    }
    bool copy_constructible() const { return m_ops.copy_construct != nullptr; }
    bool move_constructible() const { return m_ops.move_construct != nullptr; }
    bool copy_assignable() const { return m_ops.copy_assign != nullptr; }
    bool move_assignable() const { return m_ops.move_assign != nullptr; }
    bool destructible() const { return m_ops.destroy != nullptr; }

    auto operator<=>(const Type& other) const { return m_id <=> other.m_id; }
};
} // namespace fei

namespace std {
template<>
struct hash<fei::TypeId> {
    size_t operator()(const fei::TypeId& id) const {
        return static_cast<size_t>(id.id());
    }
};
template<>
struct hash<fei::Type> {
    size_t operator()(const fei::Type& type) const {
        return static_cast<size_t>(type.hash().id());
    }
};

} // namespace std
