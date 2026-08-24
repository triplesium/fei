#pragma once

#include "base/result.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting/source.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ets {

enum class LuauSystemDeclarationLayout {
    Flat,
    ScheduleGroups,
};

struct LuauCompileOptions {
    // Project scripts default to snapshot-safe behavior. The opt-out exists
    // for low-level VM tests and tooling that never participates in rollback.
    bool snapshot_safe {true};
    // Selects one exported Plugin from a value-export module. Empty selects
    // the sole Plugin and is rejected when the module exports more than one.
    std::string_view plugin_name;
};

struct LuauPluginDependency {
    std::string import_specifier;
    std::string plugin_name;
};

struct LuauScriptModuleArtifact {
    ScriptModuleDecl declaration;
    std::string bytecode;
    std::string plugin_name;
    std::vector<LuauPluginDependency> plugin_dependencies;
    bool uses_value_exports {false};
    LuauSystemDeclarationLayout system_layout {
        LuauSystemDeclarationLayout::Flat
    };
    std::vector<TypeId> required_runtime_types;
};

struct LuauScriptLibraryArtifact {
    std::string source_name;
    std::string bytecode;
};

Result<std::vector<std::string>, ScriptError>
extract_luau_script_imports(const ScriptSource& source);

Status<ScriptError> validate_luau_snapshot_safety(const ScriptSource& source);

Result<LuauScriptModuleArtifact, ScriptError> compile_luau_script_module(
    const ScriptSource& source,
    LuauCompileOptions options = {}
);

Result<LuauScriptLibraryArtifact, ScriptError> compile_luau_script_library(
    const ScriptSource& source,
    LuauCompileOptions options = {}
);

} // namespace ets
