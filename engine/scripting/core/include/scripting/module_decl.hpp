#pragma once

#include "base/optional.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei {

struct ScriptTypeRef {
    std::string type_name;
    Optional<TypeId> type_id;
    bool script_type {false};
};

struct ScriptFieldDecl {
    std::string name;
    ScriptTypeRef type;
    Val default_value;
    bool has_default {false};
};

struct ScriptTypeDecl {
    std::string name;
    std::string qualified_name;
    std::vector<ScriptFieldDecl> fields;
};

struct ScriptResourceFieldDecl {
    std::string name;
    Val value;
};

struct ScriptResourceDecl {
    std::string type;
    std::vector<ScriptResourceFieldDecl> initial_values;
    bool init_if_missing {true};
};

struct ScriptStateValueDecl {
    std::string name;
    std::uint64_t id {0};
};

struct ScriptStateDecl {
    std::string name;
    std::string qualified_name;
    TypeId type_id;
    std::string initial;
    std::vector<ScriptStateValueDecl> values;
};

struct ScriptModuleDecl {
    std::string name;
    std::string source_name;
    std::vector<ScriptTypeDecl> types;
    std::vector<ScriptResourceDecl> resources;
    std::vector<ScriptStateDecl> states;
    std::vector<DynamicSystemDecl> systems;
};

} // namespace fei
