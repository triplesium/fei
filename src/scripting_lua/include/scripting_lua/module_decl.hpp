#pragma once

#include "base/optional.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <string>
#include <vector>

namespace fei {

struct LuaScriptError {
    std::string message;
};

struct LuaScriptTypeRef {
    std::string type_name;
    Optional<TypeId> type_id;
    bool script_type {false};
};

struct LuaScriptFieldDecl {
    std::string name;
    LuaScriptTypeRef type;
    Val default_value;
    bool has_default {false};
};

struct LuaScriptTypeDecl {
    std::string name;
    std::string qualified_name;
    std::vector<LuaScriptFieldDecl> fields;
};

struct LuaScriptResourceFieldDecl {
    std::string name;
    Val value;
};

struct LuaScriptResourceDecl {
    std::string type;
    std::vector<LuaScriptResourceFieldDecl> initial_values;
    bool init_if_missing {true};
};

struct LuaScriptModuleDecl {
    std::string name;
    std::string source_name;
    std::vector<LuaScriptTypeDecl> types;
    std::vector<LuaScriptResourceDecl> resources;
    std::vector<DynamicSystemDecl> systems;
};

} // namespace fei
