#pragma once

#include "refl/type.hpp"

namespace fei {

template<class T>
class Typed {
  private:
    Type& m_type;

  public:
    using Type = T;
    Typed() : m_type(type<T>()) {}
    virtual ~Typed() = default;
    Type& type() const { return m_type; }
    TypeId type_id() const { return m_type.id(); }
};

} // namespace fei
