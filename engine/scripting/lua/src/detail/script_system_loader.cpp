#include "scripting_lua/detail/script_system_loader.hpp"

#include "ecs/dynamic/system.hpp"
#include "scripting/module_install.hpp"

namespace ets::detail {

Result<SystemAccess, LuaScriptError>
lua_script_system_access_for_decl(const DynamicSystemDecl& decl) {
    return script_system_access_for_decl(decl);
}

SystemProfileInfo lua_script_system_profile_for_decl(
    const LuaScriptModuleDecl& module_decl,
    const DynamicSystemDecl& system_decl
) {
    return script_system_profile_for_decl(module_decl, system_decl);
}

Result<std::vector<SystemHandle>, LuaScriptError> install_lua_script_systems(
    World& world,
    LuaRuntime& runtime,
    LuaScriptModuleId module,
    const LuaScriptModuleDecl& decl
) {
    auto bind_type = [&](const ScriptTypeBinding& binding) {
        return runtime
            .bind_module_type(module, binding.local_name, *binding.type);
    };
    auto create_executor = [&](const DynamicSystemDecl& system)
        -> Result<std::unique_ptr<DynamicSystemExecutor>, ScriptError> {
        return make_script_system_executor(
            [&runtime,
             module,
             name = system.name](const std::vector<Ref>& args) {
                return runtime.call_module_function(module, name, args);
            }
        );
    };
    return install_script_module(
        world,
        decl,
        bind_type,
        create_executor,
        ScriptSystemInstallOptions {.main_thread_only = true}
    );
}

} // namespace ets::detail
