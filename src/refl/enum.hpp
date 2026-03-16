#pragma once
#include "refl/type.hpp"

#include <unordered_map>

namespace fei {

class Enum {
  private:
    TypeId m_type_id;
    std::unordered_map<std::string, int64_t> m_values;

  public:
    Enum(TypeId type_id) : m_type_id(type_id) {}

    Enum(const Enum&) = delete;
    Enum& operator=(const Enum&) = delete;

    Enum(Enum&&) noexcept = default;
    Enum& operator=(Enum&&) noexcept = default;

    Enum& add_value(const std::string& name, int64_t value) {
        m_values[name] = value;
        return *this;
    }

    const std::unordered_map<std::string, int64_t>& values() const {
        return m_values;
    }

    TypeId type_id() const { return m_type_id; }
};

} // namespace fei
