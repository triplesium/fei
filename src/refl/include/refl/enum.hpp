#pragma once
#include "refl/type.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace fei {

class Val;

class Enum {
  public:
    using ConstructFunc = void (*)(void* dest, std::int64_t underlying_value);

  private:
    TypeId m_type_id;
    ConstructFunc m_construct {nullptr};
    std::unordered_map<std::string, std::int64_t> m_enumerators;

  public:
    Enum(TypeId type_id) : m_type_id(type_id) {}

    Enum(const Enum&) = delete;
    Enum& operator=(const Enum&) = delete;

    Enum(Enum&&) noexcept = default;
    Enum& operator=(Enum&&) noexcept = default;

    Enum& set_construct_func(ConstructFunc func) {
        m_construct = func;
        return *this;
    }

    Enum&
    add_enumerator(const std::string& name, std::int64_t underlying_value) {
        m_enumerators[name] = underlying_value;
        return *this;
    }

    const std::unordered_map<std::string, std::int64_t>& enumerators() const {
        return m_enumerators;
    }

    TypeId type_id() const { return m_type_id; }
    Val make_val(std::int64_t underlying_value) const;
};

bool is_enum_type(TypeId type_id);

} // namespace fei
