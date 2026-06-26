#pragma once

#include "base/result.hpp"
#include "ecs/system.hpp"
#include "ecs/system_profile.hpp"
#include "scripting/query.hpp"
#include "scripting/runtime.hpp"

#include <string>
#include <vector>

namespace fei {

class World;

struct ScriptSystemArg {
    ScriptSystemParamKind kind {ScriptSystemParamKind::Resource};
    ScriptSystemAccess access {ScriptSystemAccess::Read};
    TypeId type;
    std::string name;
    std::vector<ScriptQueryField> query_fields;
    std::vector<ScriptQueryFilter> query_filters;
};

class ScriptSystem : public System {
  private:
    ScriptRuntime* m_runtime {nullptr};
    ScriptModuleId m_module {invalid_script_module_id};
    std::string m_name;
    std::vector<ScriptSystemArg> m_args;
    SystemAccess m_access;

  public:
    ScriptSystem(
        ScriptRuntime& runtime,
        ScriptModuleId module,
        std::string name,
        std::vector<ScriptSystemArg> args,
        SystemAccess access = {}
    );

    void run(World& world) override;
    const SystemAccess& access() const override { return m_access; }
};

Result<std::vector<SystemHandle>, ScriptError> register_script_systems(
    World& world,
    ScriptRuntime& runtime,
    ScriptModuleId module,
    const ScriptModuleManifest& manifest
);

Result<SystemAccess, ScriptError>
script_system_access_for_manifest(const ScriptSystemManifest& manifest);

SystemProfileInfo script_system_profile_for_manifest(
    const ScriptModuleManifest& module_manifest,
    const ScriptSystemManifest& system_manifest
);

} // namespace fei
