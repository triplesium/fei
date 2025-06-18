#pragma once

#include "refl/type.hpp"

namespace fei {

template<typename T>
using raw_type = typename std::remove_pointer_t<std::remove_cvref_t<T>>;

export template<typename T>
const Type& type_of(T&&) {
    return type<raw_type<T>>();
}

}