#pragma once
#include "refl/type.hpp"

#include <type_traits>

namespace fei {

class QualType {
  public:
    enum Flags : unsigned int {
        None = 0,
        Pointer = (1 << 0),
        Reference = (1 << 1),
        Const = (1 << 2),
        RvalueReference = (1 << 3),
    };

    QualType(TypeId type_id, Flags flags = None) :
        m_type_id(type_id), m_flags(flags) {}
    QualType() = default;

    template<typename T>
    static QualType of() {
        using NoRef = std::remove_reference_t<T>;
        using NoPtr = std::remove_pointer_t<NoRef>;
        using Base = std::remove_cv_t<NoPtr>;

        unsigned int flags = None;
        if constexpr (std::is_pointer_v<NoRef>) {
            flags |= Pointer;
        }
        if constexpr (std::is_lvalue_reference_v<T>) {
            flags |= Reference;
        }
        if constexpr (std::is_rvalue_reference_v<T>) {
            flags |= RvalueReference;
        }
        if constexpr (std::is_const_v<NoPtr>) {
            flags |= Const;
        }
        return QualType(fei::type_id<Base>(), static_cast<Flags>(flags));
    }

    template<typename T>
    QualType() : QualType(QualType::of<T>()) {}

    bool is_pointer() const { return m_flags & Pointer; }

    bool is_reference() const { return m_flags & Reference; }

    bool is_rvalue_reference() const { return m_flags & RvalueReference; }

    bool is_const() const { return m_flags & Const; }

    TypeId type_id() const { return m_type_id; }
    unsigned int flags() const { return m_flags; }

    bool operator==(const QualType& other) const {
        return m_type_id == other.m_type_id && m_flags == other.m_flags;
    }

  private:
    TypeId m_type_id;
    unsigned int m_flags {None};
};
} // namespace fei
