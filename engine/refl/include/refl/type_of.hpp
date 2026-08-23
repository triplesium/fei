#pragma once

#include "refl/type.hpp"

namespace ets {

template<typename T>
using raw_type = typename std::remove_pointer_t<std::remove_cvref_t<T>>;

template<typename T>
Type& type_of(T&&) {
    return type<raw_type<T>>();
}

} // namespace ets
