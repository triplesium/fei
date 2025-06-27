#pragma once

#include "base/debug.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"

#include <cassert>
#include <type_traits>

namespace fei {

class Ref {
  private:
    void* m_ptr;
    TypeId m_type_id;

  public:
    Ref() : m_ptr(nullptr), m_type_id(0) {}
    Ref(std::nullptr_t) : m_ptr(nullptr), m_type_id(0) {}
    Ref(void* ptr, TypeId type) : m_ptr(ptr), m_type_id(type) {}

    template<class T>
    std::decay_t<T>& get() const {
        FEI_ASSERT(m_type_id);
        return *static_cast<std::decay_t<T>*>(m_ptr);
    }

    TypeId type_id() const {
        FEI_ASSERT(m_type_id);
        return m_type_id;
    }

    void* ptr() const { return m_ptr; }

    bool operator==(const Ref& other) const {
        return m_ptr == other.m_ptr && m_type_id == other.m_type_id;
    }

    bool operator!=(const Ref& other) const { return !(*this == other); }

    explicit operator bool() const { return m_ptr != nullptr; }
};

template<class T>
Ref make_ref(T& value) {
    return Ref(&value, type<T>().id());
}

template<class T>
Ref make_ref(const T& value) {
    return Ref(const_cast<T*>(&value), type<T>().id());
}

template<class T>
Ref make_ref(T* ptr) {
    return Ref(ptr, type<T>().id());
}

template<class T>
Ref make_ref(const T* ptr) {
    return Ref(const_cast<T*>(ptr), type<T>().id());
}

} // namespace fei
