#pragma once
#include "refl/ref.hpp"
#include "refl/registry.hpp"

#include <concepts>
#include <type_traits>

namespace fei {

inline Ref make_ref(Ref ref) {
    return ref;
}

template<class T>
    requires(!std::same_as<std::remove_cv_t<T>, Ref> && !std::is_pointer_v<T>)
Ref make_ref(T& value) {
    using U = std::remove_cv_t<T>;
    return Ref(
        const_cast<U*>(&value),
        Registry::instance().register_type<U>().id(),
        std::is_const_v<T>
    );
}

template<class T>
Ref make_ref(T* ptr) {
    if (!ptr) {
        return {};
    }
    return Ref(ptr, Registry::instance().register_type<T>().id());
}

template<class T>
Ref make_ref(const T* ptr) {
    if (!ptr) {
        return {};
    }
    return Ref(ptr, Registry::instance().register_type<T>().id());
}

} // namespace fei
