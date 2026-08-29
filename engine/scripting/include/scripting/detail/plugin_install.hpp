#pragma once

#include "base/result.hpp"
#include "ecs/dynamic/system.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system_access.hpp"
#include "ecs/system_profile.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting/schema.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ets {

class Type;
class World;

struct LuauTypeBinding {
    std::string local_name;
    std::string qualified_name;
    Type* type {nullptr};
};

using LuauTypeBindings = std::vector<LuauTypeBinding>;
using LuauTypeBinder =
    std::function<Status<LuauScriptError>(const LuauTypeBinding&)>;
using LuauSystemExecutorFactory = std::function<
    Result<std::unique_ptr<DynamicSystemExecutor>, LuauScriptError>(
        const DynamicSystemDecl&
    )>;
using LuauConditionExecutorFactory = std::function<
    Result<std::unique_ptr<DynamicConditionExecutor>, LuauScriptError>(
        const DynamicConditionDecl&
    )>;
using LuauSystemCall =
    std::function<Status<LuauScriptError>(const std::vector<Ref>&)>;
using LuauConditionCall =
    std::function<Result<bool, LuauScriptError>(const std::vector<Ref>&)>;

struct LuauSystemInstallOptions {
    bool main_thread_only {false};
    LuauConditionExecutorFactory create_condition_executor;
};

std::unique_ptr<DynamicSystemExecutor> make_luau_system_executor(
    LuauSystemCall call,
    bool checkpoint_safe_stateless = false
);

std::unique_ptr<DynamicConditionExecutor> make_luau_condition_executor(
    LuauConditionCall call,
    bool checkpoint_safe_stateless = false
);

Result<LuauTypeBindings, LuauScriptError>
ensure_luau_types(const LuauModuleSchema& schema);

Result<SystemAccess, LuauScriptError>
luau_system_access_for_decl(const DynamicSystemDecl& decl);

SystemProfileInfo luau_system_profile_for_decl(
    std::string_view source_name,
    const DynamicSystemDecl& system_decl
);

Result<std::vector<SystemHandle>, LuauScriptError> install_luau_systems(
    World& world,
    std::string_view source_name,
    const std::vector<DynamicSystemDecl>& systems,
    const LuauSystemExecutorFactory& create_executor,
    LuauSystemInstallOptions options = {}
);

bool remove_luau_plugin_systems(
    World& world,
    const std::vector<SystemHandle>& systems
);

} // namespace ets
