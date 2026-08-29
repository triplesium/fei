#pragma once

#include "base/result.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting/module_metadata.hpp"
#include "scripting/source.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ets {

using LuauModuleMetadataResolver = std::function<
    Result<std::shared_ptr<const LuauModuleMetadata>, LuauScriptError>(
        std::string_view specifier
    )>;

struct LuauCompileOptions {
    // Project scripts default to snapshot-safe behavior. The opt-out exists
    // for low-level VM tests and tooling that never participates in rollback.
    bool snapshot_safe {true};
    // Reuses the source-owned reflection metadata when an asset compilation
    // session has already analyzed this module.
    std::shared_ptr<const LuauModuleMetadata> metadata;
    // Resolves the reflection metadata for a relative require. Imported
    // system signatures are queried from the resolved module.
    LuauModuleMetadataResolver module_metadata_resolver;
};

struct LuauPropertyPathDecl {
    std::string atom_name;
    TypeId root_type;
    TypeId leaf_type;
    std::vector<std::string> properties;
};

struct LuauScriptModuleArtifact {
    std::shared_ptr<const LuauModuleMetadata> metadata;
    std::vector<LuauPluginDecl> plugins;
    std::vector<LuauFunctionDecl> functions;
    std::vector<LuauStateDecl> states;
    std::string bytecode;
    std::vector<TypeId> required_runtime_types;
    std::vector<LuauPropertyPathDecl> property_paths;

    [[nodiscard]] const LuauPluginDecl*
    find_plugin(std::string_view name) const {
        for (const auto& plugin : plugins) {
            if (plugin.name == name) {
                return &plugin;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::span<const LuauPluginDependency>
    plugin_dependencies(std::string_view plugin_name) const {
        const auto* selected = metadata->find_plugin(plugin_name);
        return selected != nullptr ?
                   std::span<const LuauPluginDependency> {
                       selected->dependencies,
                   } :
                   std::span<const LuauPluginDependency> {};
    }
};

bool is_native_luau_module(std::string_view specifier);

Result<std::vector<std::string>, LuauScriptError>
extract_luau_script_imports(const LuauScriptSource& source);

Result<LuauModuleMetadata, LuauScriptError> compile_luau_module_metadata(
    const LuauScriptSource& source,
    bool snapshot_safe = true
);

Result<LuauScriptModuleArtifact, LuauScriptError> compile_luau_script_module(
    const LuauScriptSource& source,
    LuauCompileOptions options = {}
);

} // namespace ets
