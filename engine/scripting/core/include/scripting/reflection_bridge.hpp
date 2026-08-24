#pragma once

#include "base/result.hpp"
#include "refl/callable.hpp"
#include "refl/ref.hpp"
#include "refl/val.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ets {

class Type;

struct ScriptTypeName {
    std::span<const std::string> namespace_path;
    std::string_view local_name;
};

bool is_script_visible(const Type& type);
bool is_script_prelude(const Type& type);
ScriptTypeName script_type_name(const Type& type);
std::string
script_type_path(const Type& type, std::string_view separator = ".");

Result<Val, InvokeFailure> script_default_construct(TypeId type);

Result<Val, InvokeFailure>
script_construct(TypeId type, const std::vector<Ref>& arguments);

Result<Ref, InvokeFailure>
script_get_property(Ref instance, std::string_view name);

Status<InvokeFailure>
script_set_property(Ref instance, std::string_view name, Ref value);

bool script_has_method(Ref instance, std::string_view name);

bool script_has_static_method(TypeId type, std::string_view name);

InvokeResult script_invoke_method(
    Ref instance,
    std::string_view name,
    const std::vector<Ref>& arguments
);

InvokeResult script_invoke_static_method(
    TypeId type,
    std::string_view name,
    const std::vector<Ref>& arguments
);

} // namespace ets
