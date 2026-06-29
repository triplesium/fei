#pragma once

#include "refl/val.hpp"
#include "scripting_lua/system_decl.hpp"

#include <string>
#include <vector>

namespace fei {

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
    std::vector<LuaScriptSystemDecl> systems;
};

} // namespace fei
