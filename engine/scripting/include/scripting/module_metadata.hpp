#pragma once

#include "ecs/dynamic/system_decl.hpp"
#include "scripting/schema.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ets {

struct LuauPluginDependency {
    std::string import_specifier;
    std::string plugin_name;
};

struct LuauFunctionMetadata {
    std::string name;
    std::string qualified_name;
    std::vector<DynamicSystemParamDeclPtr> system_params;
    std::string system_signature_error;

    [[nodiscard]] bool is_system_compatible() const {
        return system_signature_error.empty();
    }
};

struct LuauPluginMetadata {
    std::string name;
    std::vector<LuauPluginDependency> dependencies;
};

struct LuauExportedTypeMetadata {
    std::string name;
    bool runtime_compatible {false};
    std::string runtime_error;
};

struct LuauModuleMetadata {
    std::uint64_t source_hash {};
    LuauModuleSchema schema;
    std::vector<std::string> imports;
    std::vector<LuauFunctionMetadata> functions;
    std::vector<LuauPluginMetadata> plugins;
    std::vector<LuauExportedTypeMetadata> exported_types;

    [[nodiscard]] const LuauFunctionMetadata*
    find_function(std::string_view name) const {
        for (const auto& function : functions) {
            if (function.name == name) {
                return &function;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const LuauPluginMetadata*
    find_plugin(std::string_view name) const {
        for (const auto& plugin : plugins) {
            if (plugin.name == name) {
                return &plugin;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const LuauExportedTypeMetadata*
    find_exported_type(std::string_view name) const {
        for (const auto& type : exported_types) {
            if (type.name == name) {
                return &type;
            }
        }
        return nullptr;
    }
};

} // namespace ets
