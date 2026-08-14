#include "scripting_lua/detail/script_system_loader.hpp"

#include "ecs/dynamic/system.hpp"
#include "scripting/module_install.hpp"

#include <memory>
#include <utility>

namespace fei::detail {
namespace {

class LuaScriptSystemExecutor final : public DynamicSystemExecutor {
  private:
    LuaRuntime* m_runtime {nullptr};
    LuaScriptModuleId m_module {invalid_lua_script_module_id};
    std::string m_name;

  public:
    LuaScriptSystemExecutor(
        LuaRuntime& runtime,
        LuaScriptModuleId module,
        std::string name
    ) : m_runtime(&runtime), m_module(module), m_name(std::move(name)) {}

    Status<DynamicSystemError> execute(const std::vector<Ref>& args) override {
        auto status = m_runtime->call_module_function(m_module, m_name, args);
        if (!status) {
            return failure(
                DynamicSystemError {std::move(status.error().message)}
            );
        }
        return {};
    }
};

} // namespace

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
        std::unique_ptr<DynamicSystemExecutor> executor =
            std::make_unique<LuaScriptSystemExecutor>(
                runtime,
                module,
                system.name
            );
        return executor;
    };
    return install_script_module(world, decl, bind_type, create_executor);
}

} // namespace fei::detail
