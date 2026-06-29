#pragma once

#include "base/optional.hpp"
#include "ecs/dynamic/access.hpp"
#include "ecs/fwd.hpp"
#include "refl/type.hpp"

#include <string>
#include <vector>

namespace fei {

struct LuaScriptError {
    std::string message;
};

enum class LuaScriptSystemParamKind {
    Resource,
    Query,
    Commands,
};

enum class LuaScriptQueryParamKind {
    Component,
    Entity,
    With,
    Without,
};

struct LuaScriptTypeRef {
    std::string name;
    Optional<TypeId> id;
    bool script_type {false};
};

struct LuaScriptQueryParam {
    std::string name;
    LuaScriptTypeRef type;
    LuaScriptQueryParamKind kind {LuaScriptQueryParamKind::Component};
    DynamicParamAccess access {DynamicParamAccess::Read};
};

struct LuaScriptSystemParam {
    std::string name;
    LuaScriptTypeRef type;
    LuaScriptSystemParamKind kind {LuaScriptSystemParamKind::Resource};
    DynamicParamAccess access {DynamicParamAccess::Read};
    bool optional {false};
    std::vector<LuaScriptQueryParam> query_params;
};

struct LuaScriptSystemDecl {
    std::string name;
    std::vector<LuaScriptSystemParam> params;
    ScheduleId schedule {};
};

} // namespace fei
