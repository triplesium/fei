#pragma once

#include "base/result.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"

#include <string_view>

namespace ets {

class World;

Status<LuauScriptError> ensure_luau_enum_type(const LuauEnumDecl& enumeration);

Result<Val, LuauScriptError> make_luau_enum_value(
    const LuauEnumDecl& enumeration,
    std::string_view value_name
);

Status<LuauScriptError> ensure_luau_state_type(const LuauStateDecl& state);

Result<Val, LuauScriptError>
make_luau_state_value(const LuauStateDecl& state, std::string_view value_name);

Status<LuauScriptError> install_luau_state(
    World& world,
    const LuauStateDecl& state,
    Val initial,
    bool init_if_missing
);

bool is_luau_state_type(TypeId type);

} // namespace ets
