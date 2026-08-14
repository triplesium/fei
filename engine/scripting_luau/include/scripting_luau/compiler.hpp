#pragma once

#include "base/result.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting/source.hpp"

#include <string>

namespace fei {

struct LuauScriptModuleArtifact {
    ScriptModuleDecl declaration;
    std::string bytecode;
};

Result<LuauScriptModuleArtifact, ScriptError>
compile_luau_script_module(const ScriptSource& source);

} // namespace fei
