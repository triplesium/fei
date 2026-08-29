#pragma once

#include "base/result.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting/source.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ets {

struct LuauImportedFunctionDecl {
    std::string name;
    std::vector<DynamicSystemParamDeclPtr> params;
};

using LuauImportedFunctionResolver =
    std::function<Result<LuauImportedFunctionDecl, ScriptError>(
        std::string_view specifier,
        std::string_view export_name
    )>;

struct LuauCompileOptions {
    // Project scripts default to snapshot-safe behavior. The opt-out exists
    // for low-level VM tests and tooling that never participates in rollback.
    bool snapshot_safe {true};
    // Selects one exported Plugin from a value-export module. Empty selects
    // the sole Plugin and is rejected when the module exports more than one.
    std::string_view plugin_name;
    // Resolves an exported function referenced through a relative require,
    // for example systems.tick in app:add_system(Update, systems.tick).
    LuauImportedFunctionResolver imported_function_resolver;
};

struct LuauPluginDependency {
    std::string import_specifier;
    std::string plugin_name;
};

struct LuauPropertyPathDecl {
    std::string atom_name;
    TypeId root_type;
    TypeId leaf_type;
    std::vector<std::string> properties;
};

struct LuauScriptModuleArtifact {
    ScriptModuleDecl declaration;
    std::string bytecode;
    std::string plugin_name;
    std::vector<LuauPluginDependency> plugin_dependencies;
    std::vector<TypeId> required_runtime_types;
    std::vector<LuauPropertyPathDecl> property_paths;
};

struct LuauScriptLibraryArtifact {
    std::string source_name;
    std::string bytecode;
};

bool is_native_luau_module(std::string_view specifier);

Result<std::vector<std::string>, ScriptError>
extract_luau_script_imports(const ScriptSource& source);

Status<ScriptError> validate_luau_snapshot_safety(const ScriptSource& source);

Result<LuauImportedFunctionDecl, ScriptError> compile_luau_exported_function(
    const ScriptSource& source,
    std::string_view export_name,
    bool snapshot_safe = true
);

Result<LuauScriptModuleArtifact, ScriptError> compile_luau_script_module(
    const ScriptSource& source,
    LuauCompileOptions options = {}
);

Result<LuauScriptLibraryArtifact, ScriptError> compile_luau_script_library(
    const ScriptSource& source,
    LuauCompileOptions options = {}
);

} // namespace ets
