#pragma once

#include "base/result.hpp"
#include "base/types.hpp"
#include "scripting/compiler.hpp"
#include "scripting/error.hpp"
#include "scripting/source.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ets {

class Enum;
class Type;

enum class LuauScriptModuleId : std::uint64_t {
    Invalid = 0,
};

inline constexpr LuauScriptModuleId invalid_luau_script_module_id =
    LuauScriptModuleId::Invalid;

struct LuauScriptImportBinding {
    std::string specifier;
    LuauScriptModuleId module {invalid_luau_script_module_id};
};

struct LuauStateBuildOperation {
    Val initial;
    bool init_if_missing {true};
};

struct LuauResourceBuildOperation {
    Val value;
    bool init_if_missing {true};
};

struct LuauEventBuildOperation {
    TypeId type;
};

struct LuauSystemsBuildOperation {
    std::vector<DynamicSystemDecl> systems;
};

using LuauPluginBuildOperation = std::variant<
    LuauStateBuildOperation,
    LuauResourceBuildOperation,
    LuauEventBuildOperation,
    LuauSystemsBuildOperation>;

using LuauPluginBuildDispatch =
    std::function<Status<LuauScriptError>(LuauPluginBuildOperation operation)>;

struct LuauPlaytestDeclaration {
    std::string id;
    std::string label;
    std::string description;
    uint32 decision_ticks {1};
    uint32 minimum_ticks {1};
    uint32 maximum_ticks {1};
    bool allow_tick_override {false};
    std::string action_schema_json;
    std::string observation_schema_json;
};

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

    Result<LuauScriptModuleId, LuauScriptError> load_module(
        const LuauScriptModuleArtifact& artifact,
        std::span<const LuauScriptImportBinding> imports = {}
    );
    Status<LuauScriptError> unload_module(LuauScriptModuleId module);
    Status<LuauScriptError> bind_module_type(
        LuauScriptModuleId module,
        const std::string& name,
        const Type& type
    );
    Status<LuauScriptError> bind_module_exported_type(
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
    Status<LuauScriptError> call_module_plugin_build(
        LuauScriptModuleId module,
        std::string_view plugin_name,
        const LuauPluginBuildDispatch& dispatch
    );
    Status<LuauScriptError> call_module_function(
        LuauScriptModuleId module,
        const std::string& function_name
    );
    Status<LuauScriptError> call_module_function(
        LuauScriptModuleId module,
        const std::string& function_name,
        std::span<const Ref> args
    );
    Result<bool, LuauScriptError> call_module_condition(
        LuauScriptModuleId module,
        const std::string& function_name,
        std::span<const Ref> args
    );
    [[nodiscard]] std::span<const LuauPlaytestDeclaration>
    module_playtests(LuauScriptModuleId module) const;
    Status<LuauScriptError> begin_module_playtest_step(
        LuauScriptModuleId module,
        std::size_t playtest,
        World& world,
        std::string_view action_json
    );
    Status<LuauScriptError> end_module_playtest_step(
        LuauScriptModuleId module,
        std::size_t playtest,
        World& world
    );
    Result<std::string, LuauScriptError> observe_module_playtest(
        LuauScriptModuleId module,
        std::size_t playtest,
        World& world
    );
};

} // namespace ets
