#pragma once

#include "base/result.hpp"
#include "refl/callable.hpp"
#include "refl/ref.hpp"
#include "refl/val.hpp"

#include <string_view>
#include <vector>

namespace fei {

Result<Val, InvokeFailure> script_default_construct(TypeId type);

Result<Val, InvokeFailure>
script_construct(TypeId type, const std::vector<Ref>& arguments);

Result<Ref, InvokeFailure>
script_get_property(Ref instance, std::string_view name);

Status<InvokeFailure>
script_set_property(Ref instance, std::string_view name, Ref value);

bool script_has_method(Ref instance, std::string_view name);

InvokeResult script_invoke_method(
    Ref instance,
    std::string_view name,
    const std::vector<Ref>& arguments
);

} // namespace fei
