#pragma once

#include "refl/utils.hpp"

#include <string>
#include <type_traits>

namespace fei {

class TypeId {
  private:
    std::size_t m_id;

  public:
    TypeId() : m_id(0) {}
    TypeId(std::size_t id) : m_id(id) {}
    TypeId(const std::string& name) : m_id(std::hash<std::string> {}(name)) {}

    std::size_t id() const { return m_id; }

    auto operator<=>(const TypeId& other) const { return m_id <=> other.m_id; }
    bool operator==(const TypeId& other) const { return m_id == other.m_id; }

    operator std::size_t() const { return m_id; }
    operator bool() const { return m_id != 0; }
};

template<typename T>
inline TypeId type_id() {
    return TypeId(std::string(type_name<std::decay_t<T>>()));
}

class Type {
  public:
    using DefaultConstructFunc = void (*)(void* dest);
    using CopyConstructFunc = void (*)(void* dest, const void* src);
    using DeleteFunc = void (*)(void* ptr);

  private:
    std::string m_name;
    TypeId m_id;
    std::size_t m_size;
    DefaultConstructFunc m_default_construct {nullptr};
    CopyConstructFunc m_copy_construct {nullptr};
    DeleteFunc m_delete {nullptr};

  public:
    Type(
        std::string name,
        TypeId id,
        std::size_t size,
        DefaultConstructFunc default_construct,
        CopyConstructFunc copy_construct,
        DeleteFunc delete_func
    ) :
        m_name(std::move(name)), m_id(id), m_size(size),
        m_default_construct(default_construct),
        m_copy_construct(copy_construct), m_delete(delete_func) {}

    const std::string& name() const { return m_name; }
    TypeId hash() const { return m_id; }
    TypeId id() const { return m_id; }
    std::size_t size() const { return m_size; }
    bool is_number() const;
    bool is_integral() const;
    bool is_floating_point() const;
    std::string stripped_name() const;
    DefaultConstructFunc default_construct_func() const {
        return m_default_construct;
    }
    CopyConstructFunc copy_construct_func() const { return m_copy_construct; }
    DeleteFunc delete_func() const { return m_delete; }

    auto operator<=>(const Type& other) const { return m_id <=> other.m_id; }
};
} // namespace fei

namespace std {
template<>
struct hash<fei::TypeId> {
    size_t operator()(const fei::TypeId& id) const { return id.id(); }
};
template<>
struct hash<fei::Type> {
    size_t operator()(const fei::Type& type) const { return type.hash(); }
};

} // namespace std
