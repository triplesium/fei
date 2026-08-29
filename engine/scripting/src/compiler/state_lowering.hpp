#pragma once

#include "base/result.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "module_ir.hpp"
#include "refl/val.hpp"
#include "scripting/error.hpp"

#include <Luau/Ast.h>
#include <unordered_set>

namespace ets::detail::luau_compiler {

class StateLoweringContext final {
  public:
    StateLoweringContext(
        const ModuleIR& module,
        const LuauModuleSchema& schema,
        LuauPluginDecl& plugin
    );

    Result<TypeId, LuauScriptError> require_state(const DynamicTypeRef& type);
    Result<Val, LuauScriptError> compile_value(const Luau::AstExpr& expression);
    Status<LuauScriptError> finalize();

    const std::unordered_set<TypeId>& required_runtime_types() const;

  private:
    Result<LuauStateDecl*, LuauScriptError>
    materialize_script_state(const TypeDeclIR& type);

    const ModuleIR* m_module;
    const LuauModuleSchema* m_schema;
    LuauPluginDecl* m_plugin;
    std::unordered_set<TypeId> m_required_runtime_types;
};

class StateUsagePass final {
  public:
    static constexpr std::string_view name = "state-usage";

    Status<LuauScriptError>
    run(const LuauPluginDecl& plugin,
        StateLoweringContext& context) const;
};

} // namespace ets::detail::luau_compiler
