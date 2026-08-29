#pragma once

#include "ecs/dynamic/system_decl.hpp"
#include "scripting/schema.hpp"

#include <string>
#include <vector>

namespace ets {

struct LuauFunctionDecl {
    std::string name;
    std::vector<DynamicSystemParamDeclPtr> params;
};

struct LuauStateDecl {
    std::string name;
    std::string qualified_name;
    TypeId type_id;
    std::vector<LuauEnumValueDecl> values;
};

struct LuauPluginDecl {
    std::string name;
    std::string source_name;
    std::vector<LuauFunctionDecl> functions;
    std::vector<LuauStateDecl> states;
};

} // namespace ets
