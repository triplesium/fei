#pragma once
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cassert>
#include <variant>

namespace fei {

class LuaObject {
  private:
    std::variant<Val, Ref> m_value;

  public:
    LuaObject() = default;
    LuaObject(const Val& val) : m_value(val) {}
    LuaObject(Val&& val) : m_value(std::move(val)) {}
    LuaObject(const Ref& ref) : m_value(ref) {}

    bool is_ref() const { return std::holds_alternative<Ref>(m_value); }
    bool is_val() const { return std::holds_alternative<Val>(m_value); }

    Ref as_ref() {
        if (is_ref()) {
            return std::get<Ref>(m_value);
        } else {
            return std::get<Val>(m_value).ref();
        }
    }

    Val& as_val() {
        assert(is_val());
        return std::get<Val>(m_value);
    }
    const Val& as_val() const {
        assert(is_val());
        return std::get<Val>(m_value);
    }

    TypeId type_id() const {
        if (is_ref()) {
            return std::get<Ref>(m_value).type_id();
        } else {
            return std::get<Val>(m_value).type_id();
        }
    }
};

} // namespace fei
