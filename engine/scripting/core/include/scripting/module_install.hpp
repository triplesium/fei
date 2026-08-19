#pragma once

#include "base/result.hpp"
#include "ecs/dynamic/system.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system_access.hpp"
#include "ecs/system_profile.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fei {

class Type;
class World;

struct ScriptTypeBinding {
    std::string local_name;
    std::string qualified_name;
    Type* type {nullptr};
};

using ScriptTypeBindings = std::vector<ScriptTypeBinding>;
using ScriptTypeBinder =
    std::function<Status<ScriptError>(const ScriptTypeBinding&)>;
using ScriptSystemExecutorFactory =
    std::function<Result<std::unique_ptr<DynamicSystemExecutor>, ScriptError>(
        const DynamicSystemDecl&
    )>;
using ScriptConditionExecutorFactory = std::function<
    Result<std::unique_ptr<DynamicConditionExecutor>, ScriptError>(
        const DynamicConditionDecl&
    )>;
using ScriptSystemCall =
    std::function<Status<ScriptError>(const std::vector<Ref>&)>;
using ScriptConditionCall =
    std::function<Result<bool, ScriptError>(const std::vector<Ref>&)>;

struct ScriptSystemInstallOptions {
    bool main_thread_only {false};
    ScriptConditionExecutorFactory create_condition_executor;
};

std::unique_ptr<DynamicSystemExecutor>
make_script_system_executor(ScriptSystemCall call);

std::unique_ptr<DynamicConditionExecutor>
make_script_condition_executor(ScriptConditionCall call);

Result<ScriptTypeBindings, ScriptError>
ensure_script_module_types(const ScriptModuleDecl& decl);

Status<ScriptError>
install_script_module_resources(World& world, const ScriptModuleDecl& decl);

Result<SystemAccess, ScriptError>
script_system_access_for_decl(const DynamicSystemDecl& decl);

SystemProfileInfo script_system_profile_for_decl(
    const ScriptModuleDecl& module_decl,
    const DynamicSystemDecl& system_decl
);

Result<std::vector<SystemHandle>, ScriptError> install_script_module_systems(
    World& world,
    const ScriptModuleDecl& decl,
    const ScriptSystemExecutorFactory& create_executor,
    ScriptSystemInstallOptions options = {}
);

Result<std::vector<SystemHandle>, ScriptError> install_script_module(
    World& world,
    const ScriptModuleDecl& decl,
    const ScriptTypeBinder& bind_type,
    const ScriptSystemExecutorFactory& create_executor,
    ScriptSystemInstallOptions options = {}
);

bool remove_script_module_systems(
    World& world,
    const std::vector<SystemHandle>& systems
);

} // namespace fei
