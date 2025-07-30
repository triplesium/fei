#pragma once
#include "refl/type.hpp"

#include <type_traits>

namespace fei {

class QualType {
  public:
    enum Flags {
        None = 0,
        Pointer = (1 << 0),
        Reference = (1 << 1),
        Const = (1 << 2),
    };

    QualType(TypeId type_id, Flags flags = None) :
        m_type_id(type_id), m_flags(flags) {}

    template<typename T>
    QualType() {
        m_flags = None;
        if (std::is_pointer_v<T>)
            m_flags |= Pointer;
        if (std::is_reference_v<T>)
            m_flags |= Reference;
        if (std::is_const_v<T>)
            m_flags |= Const;
        m_type_id = type<T>();
    }

    bool is_pointer() const { return m_flags & Pointer; }

    bool is_reference() const { return m_flags & Reference; }

    bool is_const() const { return m_flags & Const; }

    TypeId type_id() const { return m_type_id; }

  private:
    TypeId m_type_id;
    unsigned int m_flags;
};
} // namespace fei
