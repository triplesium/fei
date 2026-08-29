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

struct LuauTypeName {
    std::span<const std::string> namespace_path;
    std::string_view local_name;
};

bool is_luau_visible(const Type& type);
bool is_luau_prelude(const Type& type);
LuauTypeName luau_type_name(const Type& type);
std::string luau_type_path(const Type& type, std::string_view separator = ".");

Result<Val, InvokeFailure> luau_default_construct(TypeId type);

Result<Val, InvokeFailure>
luau_construct(TypeId type, const std::vector<Ref>& arguments);

Result<Ref, InvokeFailure>
luau_get_property(Ref instance, std::string_view name);

Status<InvokeFailure>
luau_set_property(Ref instance, std::string_view name, Ref value);

bool luau_has_method(Ref instance, std::string_view name);

bool luau_has_static_method(TypeId type, std::string_view name);

InvokeResult luau_invoke_method(
    Ref instance,
    std::string_view name,
    const std::vector<Ref>& arguments
);

InvokeResult luau_invoke_static_method(
    TypeId type,
    std::string_view name,
    const std::vector<Ref>& arguments
);

} // namespace ets
