#pragma once
#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei {

class Enum;

using ScriptModuleId = std::uint64_t;

inline constexpr ScriptModuleId invalid_script_module_id = 0;

struct ScriptError {
    std::string message;
};

struct ScriptSource {
    std::string name;
    std::string content;
    std::string language;
};

enum class ScriptSystemParamKind {
    World,
    Entity,
    Resource,
    Component,
};

enum class ScriptSystemAccess {
    Read,
    Write,
};

struct ScriptSystemParam {
    std::string name;
    std::string type;
    ScriptSystemParamKind kind {ScriptSystemParamKind::Component};
    ScriptSystemAccess access {ScriptSystemAccess::Read};
};

struct ScriptSystemManifest {
    std::string name;
    std::vector<ScriptSystemParam> params;
    ScheduleId schedule {};
};

struct ScriptModuleManifest {
    std::vector<ScriptSystemManifest> systems;
};

class ScriptRuntime {
  public:
    virtual ~ScriptRuntime();

    // TODO: register Cls instead of Type once scripting has a language-neutral
    // class binding model.
    virtual void register_type(Type& type) = 0;
    virtual void unregister_type(Type& type) = 0;
    virtual void register_enum(const Enum& enm) = 0;
    virtual void unregister_enum(const Enum& enm) = 0;
    virtual void set_global(const std::string& name, const Val& val) = 0;
    virtual void set_global(const std::string& name, const Ref& ref) = 0;
    virtual void unset_global(const std::string& name) = 0;

    virtual void run_script(const std::string& script) = 0;
    virtual bool call_function(
        const std::string& func_name,
        const std::vector<Ref>& args
    ) = 0;
    virtual Result<ScriptModuleId, ScriptError>
    load_module(const ScriptSource& source) = 0;
    virtual Status<ScriptError> unload_module(ScriptModuleId module) = 0;
    virtual Result<ScriptModuleManifest, ScriptError>
    module_manifest(ScriptModuleId module) = 0;
    virtual Status<ScriptError> call_module_function(
        ScriptModuleId module,
        const std::string& func_name,
        const std::vector<Ref>& args
    ) = 0;
};

} // namespace fei
