#pragma once
#include "refl/ref.hpp"
#include "refl/registry.hpp"

#include <concepts>

namespace fei {

inline Ref make_ref(Ref ref) {
    return ref;
}

template<class T>
    requires(!std::same_as<T, Ref>)
Ref make_ref(T& value) {
    return Ref(&value, Registry::instance().register_type<T>().id());
}

template<class T>
    requires(!std::same_as<T, Ref>)
Ref make_ref(const T& value) {
    return Ref(
        const_cast<T*>(&value),
        Registry::instance().register_type<T>().id()
    );
}

template<class T>
Ref make_ref(T* ptr) {
    return Ref(ptr, Registry::instance().register_type<T>().id());
}

template<class T>
Ref make_ref(const T* ptr) {
    return Ref(
        const_cast<T*>(ptr),
        Registry::instance().register_type<T>().id()
    );
}

} // namespace fei
