#pragma once

#include "base/optional.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ets {

struct LuauTypeRef {
    std::string type_name;
    Optional<TypeId> type_id;
    bool script_type {false};
    bool optional {false};
};

struct LuauFieldDecl {
    std::string name;
    LuauTypeRef type;
    Val default_value;
    bool has_default {false};
};

struct LuauTypeDecl {
    std::string name;
    std::string qualified_name;
    std::vector<LuauFieldDecl> fields;
};

struct LuauEnumValueDecl {
    std::string name;
    std::uint64_t id {0};
};

using LuauStateValueDecl = LuauEnumValueDecl;

struct LuauEnumDecl {
    std::string name;
    std::string qualified_name;
    TypeId type_id;
    std::vector<LuauEnumValueDecl> values;
};

struct LuauModuleSchema {
    std::string name;
    std::string source_name;
    std::vector<LuauTypeDecl> types;
    std::vector<LuauEnumDecl> enums;
};

} // namespace ets
