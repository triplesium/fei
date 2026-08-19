#pragma once

#include "base/result.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"

#include <string_view>

namespace fei {

class World;

Status<ScriptError> ensure_script_state_type(const ScriptStateDecl& state);

Result<Val, ScriptError> make_script_state_value(
    const ScriptStateDecl& state,
    std::string_view value_name
);

Status<ScriptError>
install_script_module_states(World& world, const ScriptModuleDecl& module);

bool is_script_state_type(TypeId type);

} // namespace fei
