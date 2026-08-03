#pragma once

#include "base/result.hpp"
#include "refl/type.hpp"

#include <string>
#include <string_view>

namespace fei {

class Type;

namespace devtools {

struct TypeSelectorError {
    int status {500};
    std::string message;
};

Result<Type&, TypeSelectorError>
resolve_type_selector(std::string_view selector);

std::string format_type_id(TypeId id);

} // namespace devtools

} // namespace fei
