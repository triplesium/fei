#pragma once

#include "scripting/compiler.hpp"

#include <Luau/Ast.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ets::detail::luau_compiler {

struct SourcePatch {
    Luau::Location location;
    std::string replacement;
    std::string owner;
};

class SourcePatchSet {
  public:
    void
    add(Luau::Location location,
        std::string replacement,
        std::string_view owner);

    Result<std::string, LuauScriptError> apply(const LuauScriptSource& source) const;

  private:
    std::vector<SourcePatch> m_patches;
};

class TypeQualificationPass {
  public:
    static constexpr std::string_view name = "type-qualification";

    void run(
        const LuauScriptSource& source,
        const Luau::AstStatBlock& root,
        LuauModuleSchema& schema,
        std::vector<LuauFunctionDecl>& functions,
        std::span<const LuauStateDecl> states = {}
    ) const;

    void
    run(const LuauScriptSource& source,
        const Luau::AstStatBlock& root,
        LuauModuleSchema& schema,
        LuauPluginDecl& plugin) const;
};

struct PropertyLoweringResult {
    std::vector<LuauPropertyPathDecl> property_paths;
    SourcePatchSet source_patches;
    std::vector<LuauOptimizationPassReport> optimization_report;
};

class PropertyLoweringPass {
  public:
    static constexpr std::string_view name = "property-lowering";

    PropertyLoweringResult
    run(const Luau::AstStatBlock& root,
        const std::vector<LuauFunctionDecl>& functions,
        LuauOptimizationPasses optimization_passes = {}) const;
};

class RuntimeSourceEmissionPass {
  public:
    static constexpr std::string_view name = "runtime-source-emission";

    Result<std::string, LuauScriptError>
    run(const LuauScriptSource& source,
        std::span<const std::string> functions,
        SourcePatchSet patches = {}) const;
};

} // namespace ets::detail::luau_compiler
