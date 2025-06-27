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

template<class T>
TypeId type_id() {
    return TypeId(std::string(type_name<std::decay_t<T>>()));
}

class Type {
  private:
    std::string m_name;
    TypeId m_id;
    std::size_t m_size;

  public:
    Type(std::string name, TypeId id, std::size_t size) :
        m_name(std::move(name)), m_id(id), m_size(size) {}

    const std::string& name() const { return m_name; }
    TypeId hash() const { return m_id; }
    TypeId id() const { return m_id; }
    std::size_t size() const { return m_size; }

    auto operator<=>(const Type&) const = default;
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
