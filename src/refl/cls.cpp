#pragma once
#include "refl/cls.hpp"
#include "refl/registry.hpp"

namespace fei {

Cls& Cls::set_to_string(ToStringFunc func) {
    m_to_string_func = func;
    return *this;
}

std::string Cls::to_string(Ref ref) const {
    if (m_to_string_func) {
        return m_to_string_func(ref);
    }
    return type_name(m_type_id);
}

} // namespace fei
