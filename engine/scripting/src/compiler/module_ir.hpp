#pragma once

#include "compilation_session.hpp"
#include "scripting/module_metadata.hpp"
#include "scripting/schema.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ets::detail::luau_compiler {

using ModuleImportBindings =
    std::unordered_map<const Luau::AstLocal*, std::string>;

struct RecordTypeIR {
    std::vector<LuauFieldDecl> fields;
};

struct StringUnionTypeIR {
    std::vector<std::string> values;
};

using TypeExprIR = std::variant<RecordTypeIR, StringUnionTypeIR>;

struct TypeDeclIR {
    std::string name;
    std::string qualified_name;
    TypeExprIR value;
};

struct FunctionIR {
    std::string name;
    const Luau::AstLocal* local {nullptr};
    Luau::AstExprFunction* expression {nullptr};
    bool exported {false};
};

struct PluginDependencyIR {
    std::string import_specifier;
    std::string plugin_name;
};

struct PluginIR {
    std::string name;
    const Luau::AstLocal* local {nullptr};
    const Luau::AstExprTable* descriptor {nullptr};
    const Luau::AstExprFunction* build {nullptr};
    std::vector<PluginDependencyIR> dependencies;
};

class ModuleIR final {
  public:
    ModuleIR(ModuleIR&&) noexcept = default;
    ModuleIR& operator=(ModuleIR&&) noexcept = default;
    ModuleIR(const ModuleIR&) = delete;
    ModuleIR& operator=(const ModuleIR&) = delete;

    const std::string& name() const;
    const std::string& source_name() const;
    const ModuleImportBindings& imports() const;
    const std::vector<FunctionIR>& functions() const;
    const FunctionIR* find_function(const Luau::AstLocal* local) const;
    const std::vector<PluginIR>& plugins() const;
    const std::vector<TypeDeclIR>& types() const;
    const TypeDeclIR* find_type(std::string_view name) const;

  private:
    friend class ModuleFrontendPass;

    ModuleIR() = default;

    std::string m_name;
    std::string m_source_name;
    ModuleImportBindings m_imports;
    std::vector<FunctionIR> m_functions;
    std::unordered_map<const Luau::AstLocal*, std::size_t> m_function_lookup;
    std::vector<PluginIR> m_plugins;
    std::vector<TypeDeclIR> m_types;
    std::unordered_map<std::string, std::size_t> m_type_lookup;
};

class ModuleFrontendPass final {
  public:
    static constexpr std::string_view name = "module-frontend";

    Result<ModuleIR, LuauScriptError> run(const ParsedModule& parsed) const;
};

class ModuleSchemaLoweringPass final {
  public:
    static constexpr std::string_view name = "module-declaration-lowering";

    Result<LuauModuleSchema, LuauScriptError> run(const ModuleIR& module) const;
};

class ModuleMetadataPass final {
  public:
    static constexpr std::string_view name = "module-metadata";

    Result<LuauModuleMetadata, LuauScriptError> run(
        const LuauScriptSource& source,
        const Luau::AstStatBlock& root,
        const ModuleIR& module,
        LuauModuleSchema schema
    ) const;
};

} // namespace ets::detail::luau_compiler
