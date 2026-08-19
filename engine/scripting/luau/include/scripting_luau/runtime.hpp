#pragma once

#include "base/result.hpp"
#include "scripting/error.hpp"
#include "scripting/source.hpp"
#include "scripting_luau/compiler.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace fei {

class Enum;
class Type;

using LuauScriptError = ScriptError;
using LuauScriptSource = ScriptSource;

enum class LuauScriptModuleId : std::uint64_t {
    Invalid = 0,
};

inline constexpr LuauScriptModuleId invalid_luau_script_module_id =
    LuauScriptModuleId::Invalid;

class LuauRuntime {
  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

  public:
    LuauRuntime();
    ~LuauRuntime();

    LuauRuntime(const LuauRuntime&) = delete;
    LuauRuntime& operator=(const LuauRuntime&) = delete;
    LuauRuntime(LuauRuntime&&) noexcept;
    LuauRuntime& operator=(LuauRuntime&&) noexcept;

    Status<LuauScriptError> run_script(const LuauScriptSource& source);
    Result<LuauScriptModuleId, LuauScriptError>
    load_module(const LuauScriptModuleArtifact& artifact);
    Status<LuauScriptError> unload_module(LuauScriptModuleId module);
    Status<LuauScriptError> bind_module_type(
        LuauScriptModuleId module,
        const std::string& name,
        const Type& type
    );
    Status<LuauScriptError>
    bind_module_script_type(LuauScriptModuleId module, const Type& type);
    Status<LuauScriptError> bind_module_enum(
        LuauScriptModuleId module,
        const std::string& name,
        const Enum& enm
    );
    Status<LuauScriptError>
    bind_module_script_enum(LuauScriptModuleId module, const Enum& enm);
    Status<LuauScriptError>
    seal_module_script_namespaces(LuauScriptModuleId module);
    Status<LuauScriptError> call_module_function(
        LuauScriptModuleId module,
        const std::string& function_name
    );
    Status<LuauScriptError> call_module_function(
        LuauScriptModuleId module,
        const std::string& function_name,
        std::span<const Ref> args
    );
};

} // namespace fei
