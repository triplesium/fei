#pragma once

#include "base/result.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting/source.hpp"

#include <string>
#include <vector>

namespace fei {

enum class LuauSystemDeclarationLayout {
    Flat,
    ScheduleGroups,
};

struct LuauScriptModuleArtifact {
    ScriptModuleDecl declaration;
    std::string bytecode;
    LuauSystemDeclarationLayout system_layout {
        LuauSystemDeclarationLayout::Flat
    };
    std::vector<TypeId> required_runtime_types;
};

Result<LuauScriptModuleArtifact, ScriptError>
compile_luau_script_module(const ScriptSource& source);

} // namespace fei
