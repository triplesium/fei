#include "project_scripting_luau/playtest.hpp"

#include "app/app.hpp"
#include "asset/server.hpp"
#include "ecs/dynamic/world.hpp"
#include "project/project.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "runtime_protocol/playtest.hpp"
#include "scripting/borrow_scope.hpp"
#include "scripting/source.hpp"
#include "scripting_luau/compiler.hpp"
#include "scripting_luau/detail/binding.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei::project_runtime {
namespace {

using Json = nlohmann::json;
using runtime_protocol::PlaytestError;
using runtime_protocol::PlaytestErrorKind;

constexpr std::size_t c_max_luau_json_depth = 32;
constexpr std::size_t c_max_luau_json_nodes = std::size_t {16} * 1024;

struct LoadedPlaytestDeclaration {
    runtime_protocol::PlaytestInterfaceDescriptor descriptor;
    std::size_t module {0};
};

std::string luau_error(lua_State* state, std::string fallback) {
    const char* message = lua_tostring(state, -1);
    return message != nullptr ? std::string {message} : std::move(fallback);
}

int absolute_index(lua_State* state, int index) {
    return index > 0 || index <= LUA_REGISTRYINDEX ?
               index :
               lua_gettop(state) + index + 1;
}

int playtest_helper(lua_State* state) {
    if (lua_gettop(state) != 1 || !lua_istable(state, 1)) {
        luaL_error(state, "playtest expects exactly one declaration table");
    }
    lua_pushboolean(state, true);
    lua_setfield(state, 1, "__fei_playtest");
    lua_pushvalue(state, 1);
    return 1;
}

int query_descriptor_helper(lua_State* state) {
    luaL_checktype(state, 1, LUA_TUSERDATA);
    const char* kind = lua_tostring(state, lua_upvalueindex(1));
    lua_newtable(state);
    lua_pushstring(state, kind);
    lua_setfield(state, -2, "kind");
    lua_pushvalue(state, 1);
    lua_setfield(state, -2, "type");
    return 1;
}

bool valid_identifier(std::string_view name) {
    if (name.empty() ||
        (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
         name.front() != '_')) {
        return false;
    }
    return std::ranges::all_of(name.substr(1), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
               character == '_';
    });
}

std::string local_type_name(const Type& type) {
    const auto position = type.name().find_last_of(".:");
    return position == std::string::npos ? type.name() :
                                           type.name().substr(position + 1);
}

void install_playtest_helpers(lua_State* state) {
    lua_pushcfunction(state, playtest_helper, "playtest");
    lua_setglobal(state, "playtest");

    const char* query_descriptors[] = {"Read", "Write", "With", "Without"};
    for (const char* name : query_descriptors) {
        std::string kind {name};
        kind[0] = static_cast<char>(std::tolower(kind[0]));
        lua_pushlstring(state, kind.data(), kind.size());
        lua_pushcclosure(state, query_descriptor_helper, name, 1);
        lua_setglobal(state, name);
    }
    lua_newtable(state);
    lua_pushstring(state, "entity");
    lua_setfield(state, -2, "kind");
    lua_setglobal(state, "Entity");
}

void bind_reflected_globals(lua_State* state) {
    static const std::unordered_set<std::string_view> reserved {
        "Entity",
        "Read",
        "Write",
        "With",
        "Without",
        "playtest",
    };
    std::unordered_map<std::string, std::size_t> name_counts;
    for (const auto& [id, type] : Registry::instance().types()) {
        (void)id;
        ++name_counts[local_type_name(type)];
    }
    for (const auto& [id, type] : Registry::instance().types()) {
        const auto name = local_type_name(type);
        if (name_counts[name] != 1 ||
            Registry::instance().enums().contains(id) ||
            reserved.contains(name) || !valid_identifier(name)) {
            continue;
        }
        fei::detail::push_luau_type_token(state, id);
        lua_setglobal(state, name.c_str());
    }
    for (const auto& [id, enm] : Registry::instance().enums()) {
        auto type = Registry::instance().try_get_type(id);
        if (!type) {
            continue;
        }
        const auto name = local_type_name(*type);
        if (name_counts[name] != 1 || reserved.contains(name) ||
            !valid_identifier(name)) {
            continue;
        }
        lua_newtable(state);
        for (const auto& [enumerator, underlying_value] : enm.enumerators()) {
            fei::detail::push_luau_owned_value(
                state,
                enm.make_val(underlying_value)
            );
            lua_setfield(state, -2, enumerator.c_str());
        }
        lua_setreadonly(state, -1, true);
        lua_setglobal(state, name.c_str());
    }
}

void bind_declared_type_aliases(lua_State* state, int declaration) {
    lua_getfield(state, declaration, "types");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        throw std::runtime_error("playtest field 'types' must be a table");
    }

    const int aliases = absolute_index(state, -1);
    lua_pushnil(state);
    while (lua_next(state, aliases) != 0) {
        if (!lua_isstring(state, -2) || !lua_isstring(state, -1)) {
            lua_pop(state, 1);
            throw std::runtime_error(
                "playtest type aliases must map names to qualified type names"
            );
        }
        std::size_t alias_size = 0;
        const char* alias_value = lua_tolstring(state, -2, &alias_size);
        std::string alias(alias_value, alias_size);
        if (!valid_identifier(alias) || alias == "playtest" ||
            alias == "Read" || alias == "Write" || alias == "With" ||
            alias == "Without" || alias == "Entity") {
            lua_pop(state, 1);
            throw std::runtime_error(
                "Invalid playtest type alias '" + alias + "'"
            );
        }

        std::size_t type_size = 0;
        const char* type_value = lua_tolstring(state, -1, &type_size);
        const std::string type_name(type_value, type_size);
        auto type = Registry::instance().try_get_type(type_name);
        if (!type) {
            lua_pop(state, 1);
            throw std::runtime_error(
                "Unknown playtest type '" + type_name + "'"
            );
        }
        const auto enm = Registry::instance().enums().find(type->id());
        if (enm == Registry::instance().enums().end()) {
            fei::detail::push_luau_type_token(state, type->id());
        } else {
            lua_newtable(state);
            for (const auto& [enumerator, underlying_value] :
                 enm->second.enumerators()) {
                fei::detail::push_luau_owned_value(
                    state,
                    enm->second.make_val(underlying_value)
                );
                lua_setfield(state, -2, enumerator.c_str());
            }
            lua_setreadonly(state, -1, true);
        }
        lua_setglobal(state, alias.c_str());
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
}

Json luau_json(
    lua_State* state,
    int index,
    std::string_view context,
    std::size_t depth,
    std::size_t& nodes
) {
    if (depth > c_max_luau_json_depth || ++nodes > c_max_luau_json_nodes) {
        throw std::runtime_error(
            std::string(context) + " exceeds the JSON complexity limit"
        );
    }

    switch (lua_type(state, index)) {
        case LUA_TBOOLEAN:
            return lua_toboolean(state, index) != 0;
        case LUA_TNUMBER: {
            const auto value = lua_tonumber(state, index);
            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    std::string(context) + " contains a non-finite number"
                );
            }
            if (std::trunc(value) == value &&
                value >= static_cast<double>(
                             std::numeric_limits<std::int64_t>::min()
                         ) &&
                value <= static_cast<double>(
                             std::numeric_limits<std::int64_t>::max()
                         )) {
                return static_cast<std::int64_t>(value);
            }
            return value;
        }
        case LUA_TSTRING: {
            std::size_t size = 0;
            const char* value = lua_tolstring(state, index, &size);
            return std::string(value, size);
        }
        case LUA_TTABLE:
            break;
        default:
            throw std::runtime_error(
                std::string(context) +
                " must contain only booleans, numbers, strings, and tables"
            );
    }

    const int table_index = absolute_index(state, index);
    const auto array_size =
        static_cast<std::size_t>(lua_objlen(state, table_index));
    std::size_t entry_count = 0;
    bool has_string_key = false;
    bool has_numeric_key = false;
    lua_pushnil(state);
    while (lua_next(state, table_index) != 0) {
        ++entry_count;
        if (lua_type(state, -2) == LUA_TSTRING) {
            has_string_key = true;
        } else if (lua_type(state, -2) == LUA_TNUMBER) {
            const auto key = lua_tonumber(state, -2);
            if (std::trunc(key) != key || key < 1.0 ||
                key > static_cast<double>(array_size)) {
                lua_pop(state, 1);
                throw std::runtime_error(
                    std::string(context) +
                    " contains an invalid numeric table key"
                );
            }
            has_numeric_key = true;
        } else {
            lua_pop(state, 1);
            throw std::runtime_error(
                std::string(context) + " contains an unsupported table key"
            );
        }
        lua_pop(state, 1);
    }

    if (array_size > 0) {
        if (has_string_key || !has_numeric_key || entry_count != array_size) {
            throw std::runtime_error(
                std::string(context) +
                " cannot mix array entries with object fields"
            );
        }
        Json result = Json::array();
        for (std::size_t item = 1; item <= array_size; ++item) {
            lua_rawgeti(state, table_index, static_cast<int>(item));
            result.push_back(luau_json(state, -1, context, depth + 1, nodes));
            lua_pop(state, 1);
        }
        return result;
    }
    if (has_numeric_key) {
        throw std::runtime_error(
            std::string(context) + " contains a sparse array"
        );
    }

    Json result = Json::object();
    lua_pushnil(state);
    while (lua_next(state, table_index) != 0) {
        std::size_t key_size = 0;
        const char* key = lua_tolstring(state, -2, &key_size);
        result[std::string(key, key_size)] =
            luau_json(state, -1, context, depth + 1, nodes);
        lua_pop(state, 1);
    }
    return result;
}

Json luau_json(lua_State* state, int index, std::string_view context) {
    std::size_t nodes = 0;
    return luau_json(state, index, context, 0, nodes);
}

void push_json(lua_State* state, const Json& value, std::size_t depth = 0) {
    if (depth > c_max_luau_json_depth) {
        throw std::runtime_error("Action exceeds the JSON depth limit");
    }
    if (value.is_null()) {
        throw std::runtime_error("Luau playtest actions do not support null");
    }
    if (value.is_boolean()) {
        lua_pushboolean(state, value.get<bool>());
        return;
    }
    if (value.is_number_integer()) {
        lua_pushinteger(state, value.get<lua_Integer>());
        return;
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number <= static_cast<std::uint64_t>(
                          std::numeric_limits<lua_Integer>::max()
                      )) {
            lua_pushinteger(state, static_cast<lua_Integer>(number));
        } else {
            lua_pushnumber(state, static_cast<double>(number));
        }
        return;
    }
    if (value.is_number_float()) {
        lua_pushnumber(state, value.get<double>());
        return;
    }
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        lua_pushlstring(state, text.data(), text.size());
        return;
    }
    lua_newtable(state);
    if (value.is_array()) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            push_json(state, value[index], depth + 1);
            lua_rawseti(state, -2, static_cast<int>(index + 1));
        }
        return;
    }
    if (value.is_object()) {
        for (const auto& [key, item] : value.items()) {
            push_json(state, item, depth + 1);
            lua_setfield(state, -2, key.c_str());
        }
        return;
    }
    throw std::runtime_error("Action contains an unsupported JSON value");
}

std::string
required_string_field(lua_State* state, int table, const char* field) {
    lua_getfield(state, table, field);
    if (!lua_isstring(state, -1)) {
        lua_pop(state, 1);
        throw std::runtime_error(
            std::string("playtest field '") + field + "' must be a string"
        );
    }
    std::size_t size = 0;
    const char* value = lua_tolstring(state, -1, &size);
    std::string result(value, size);
    lua_pop(state, 1);
    return result;
}

std::string optional_string_field(
    lua_State* state,
    int table,
    const char* field,
    std::string fallback
) {
    lua_getfield(state, table, field);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    if (!lua_isstring(state, -1)) {
        lua_pop(state, 1);
        throw std::runtime_error(
            std::string("playtest field '") + field + "' must be a string"
        );
    }
    std::size_t size = 0;
    const char* value = lua_tolstring(state, -1, &size);
    std::string result(value, size);
    lua_pop(state, 1);
    return result;
}

std::uint32_t optional_uint_field(
    lua_State* state,
    int table,
    const char* field,
    std::uint32_t fallback
) {
    lua_getfield(state, table, field);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    if (!lua_isnumber(state, -1)) {
        lua_pop(state, 1);
        throw std::runtime_error(
            std::string("playtest ticks field '") + field +
            "' must be an integer"
        );
    }
    const auto value = lua_tonumber(state, -1);
    lua_pop(state, 1);
    if (std::trunc(value) != value || value < 1.0 ||
        value >
            static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error(
            std::string("playtest ticks field '") + field +
            "' must be a positive 32-bit integer"
        );
    }
    return static_cast<std::uint32_t>(value);
}

bool optional_bool_field(
    lua_State* state,
    int table,
    const char* field,
    bool fallback
) {
    lua_getfield(state, table, field);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    if (!lua_isboolean(state, -1)) {
        lua_pop(state, 1);
        throw std::runtime_error(
            std::string("playtest ticks field '") + field +
            "' must be a boolean"
        );
    }
    const bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

std::string schema_field(lua_State* state, int table, const char* field) {
    lua_getfield(state, table, field);
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        throw std::runtime_error(
            std::string("playtest field '") + field +
            "' must be a JSON Schema table"
        );
    }
    auto schema = luau_json(state, -1, field).dump();
    lua_pop(state, 1);
    return schema;
}

int function_field(
    lua_State* state,
    int table,
    const char* field,
    bool required
) {
    lua_getfield(state, table, field);
    if (lua_isnil(state, -1) && !required) {
        lua_pop(state, 1);
        return 0;
    }
    if (!lua_isfunction(state, -1)) {
        lua_pop(state, 1);
        throw std::runtime_error(
            std::string("playtest field '") + field + "' must be a function"
        );
    }
    const int reference = lua_ref(state, -1);
    lua_pop(state, 1);
    return reference;
}

class LuauPlaytestRuntime {
  private:
    struct Module {
        lua_State* thread {nullptr};
        int thread_ref {0};
        int begin_step {0};
        int end_step {0};
        int observe {0};
        std::string source_name;
    };

    struct Impl {
        lua_State* state {nullptr};
        std::vector<Module> modules;
        ScriptBorrowScope borrow_scope;

        Impl() : state(luaL_newstate()) {
            if (state == nullptr) {
                throw std::runtime_error("Failed to create Luau playtest VM");
            }
            luaL_openlibs(state);
            fei::detail::install_luau_borrowed_object_metatable(state);
            luaL_sandbox(state);
        }

        ~Impl() {
            if (state != nullptr) {
                lua_close(state);
            }
        }
    };

    std::unique_ptr<Impl> m_impl;

    Module* module(std::size_t id) {
        return id < m_impl->modules.size() ? &m_impl->modules[id] : nullptr;
    }

    Status<PlaytestError> call_without_result(
        std::size_t module_id,
        int Module::* function,
        World& world,
        const Json* action,
        PlaytestErrorKind error_kind,
        std::string_view callback
    ) {
        auto* loaded = module(module_id);
        if (loaded == nullptr) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = "Luau playtest module is not loaded",
                }
            );
        }
        const int function_ref = loaded->*function;
        if (function_ref == 0) {
            return {};
        }

        DynamicWorld context("playtest." + std::string(callback));
        auto prepared = context.prepare(world);
        if (!prepared) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = std::move(prepared.error().message),
                }
            );
        }

        auto* thread = loaded->thread;
        auto& scope = m_impl->borrow_scope;
        const auto token = scope.begin();
        lua_settop(thread, 0);
        lua_getref(thread, function_ref);
        fei::detail::push_luau_borrowed_ref(thread, *prepared, scope, token);
        int argument_count = 1;
        try {
            if (action != nullptr) {
                push_json(thread, *action);
                ++argument_count;
            }
        } catch (const std::exception& error) {
            scope.end(token);
            context.finish();
            lua_settop(thread, 0);
            return failure(
                PlaytestError {
                    .kind = error_kind,
                    .message = error.what(),
                }
            );
        }
        if (lua_pcall(thread, argument_count, 0, 0) != 0) {
            auto message =
                luau_error(thread, "Failed to call Luau playtest callback");
            scope.end(token);
            context.finish();
            lua_settop(thread, 0);
            return failure(
                PlaytestError {
                    .kind = error_kind,
                    .message = std::string(callback) + " failed: " + message,
                }
            );
        }
        scope.end(token);
        context.finish();
        lua_settop(thread, 0);
        return {};
    }

  public:
    LuauPlaytestRuntime() : m_impl(std::make_unique<Impl>()) {}
    ~LuauPlaytestRuntime() = default;
    LuauPlaytestRuntime(LuauPlaytestRuntime&&) noexcept = default;
    LuauPlaytestRuntime& operator=(LuauPlaytestRuntime&&) noexcept = default;
    LuauPlaytestRuntime(const LuauPlaytestRuntime&) = delete;
    LuauPlaytestRuntime& operator=(const LuauPlaytestRuntime&) = delete;

    Result<LoadedPlaytestDeclaration, PlaytestError>
    load(const ScriptSource& source) {
        auto snapshot_safe = validate_luau_snapshot_safety(source);
        if (!snapshot_safe) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::InvalidAction,
                    .message = std::move(snapshot_safe.error().message),
                }
            );
        }
        auto* root = m_impl->state;
        const int root_top = lua_gettop(root);
        lua_State* thread = lua_newthread(root);
        const int thread_ref = lua_ref(root, -1);
        lua_pop(root, 1);
        luaL_sandboxthread(thread);
        install_playtest_helpers(thread);
        bind_reflected_globals(thread);

        std::size_t bytecode_size = 0;
        char* bytecode = luau_compile(
            source.content.data(),
            source.content.size(),
            nullptr,
            &bytecode_size
        );
        if (bytecode == nullptr) {
            lua_unref(root, thread_ref);
            lua_settop(root, root_top);
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::InvalidAction,
                    .message = source.name + ": Luau compilation failed",
                }
            );
        }
        const int load_status =
            luau_load(thread, source.name.c_str(), bytecode, bytecode_size, 0);
        std::free(bytecode);
        if (load_status != 0 || lua_pcall(thread, 0, 1, 0) != 0) {
            auto message = luau_error(
                thread,
                "Failed to execute Luau playtest declaration"
            );
            lua_unref(root, thread_ref);
            lua_settop(root, root_top);
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::InvalidAction,
                    .message = source.name + ": " + message,
                }
            );
        }

        try {
            if (!lua_istable(thread, -1)) {
                throw std::runtime_error("script must return playtest { ... }");
            }
            const int declaration = absolute_index(thread, -1);
            lua_getfield(thread, declaration, "__fei_playtest");
            const bool tagged = lua_toboolean(thread, -1) != 0;
            lua_pop(thread, 1);
            if (!tagged) {
                throw std::runtime_error("script must return playtest { ... }");
            }

            bind_declared_type_aliases(thread, declaration);

            auto id = required_string_field(thread, declaration, "id");
            auto label =
                optional_string_field(thread, declaration, "label", id);
            auto description = optional_string_field(
                thread,
                declaration,
                "description",
                "Project playtest interface " + id
            );

            std::uint32_t decision_ticks = 1;
            std::uint32_t minimum_ticks = 1;
            std::uint32_t maximum_ticks = 1;
            bool allow_tick_override = false;
            lua_getfield(thread, declaration, "ticks");
            if (!lua_isnil(thread, -1)) {
                if (!lua_istable(thread, -1)) {
                    lua_pop(thread, 1);
                    throw std::runtime_error(
                        "playtest field 'ticks' must be a table"
                    );
                }
                const int ticks = absolute_index(thread, -1);
                decision_ticks =
                    optional_uint_field(thread, ticks, "default", 1);
                minimum_ticks =
                    optional_uint_field(thread, ticks, "min", decision_ticks);
                maximum_ticks =
                    optional_uint_field(thread, ticks, "max", decision_ticks);
                allow_tick_override =
                    optional_bool_field(thread, ticks, "overridable", false);
            }
            lua_pop(thread, 1);

            auto action_schema = schema_field(thread, declaration, "action");
            std::string observation_schema =
                R"({"type":"object","additionalProperties":false})";
            lua_getfield(thread, declaration, "observation");
            const bool has_observation = !lua_isnil(thread, -1);
            lua_pop(thread, 1);
            if (has_observation) {
                observation_schema =
                    schema_field(thread, declaration, "observation");
            }

            Module module {
                .thread = thread,
                .thread_ref = thread_ref,
                .begin_step =
                    function_field(thread, declaration, "begin_step", true),
                .end_step =
                    function_field(thread, declaration, "end_step", false),
                .observe =
                    function_field(thread, declaration, "observe", false),
                .source_name = source.name,
            };
            const auto module_id = m_impl->modules.size();
            m_impl->modules.push_back(std::move(module));
            lua_settop(thread, 0);
            lua_settop(root, root_top);
            return LoadedPlaytestDeclaration {
                .descriptor =
                    runtime_protocol::PlaytestInterfaceDescriptor {
                        .id = std::move(id),
                        .label = std::move(label),
                        .description = std::move(description),
                        .decision_ticks = decision_ticks,
                        .minimum_ticks = minimum_ticks,
                        .maximum_ticks = maximum_ticks,
                        .allow_tick_override = allow_tick_override,
                        .action_schema_json = std::move(action_schema),
                        .observation_schema_json =
                            std::move(observation_schema),
                    },
                .module = module_id,
            };
        } catch (const std::exception& error) {
            lua_unref(root, thread_ref);
            lua_settop(root, root_top);
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::InvalidAction,
                    .message = source.name + ": " + error.what(),
                }
            );
        }
    }

    Status<PlaytestError>
    begin_step(std::size_t module, World& world, std::string_view action_json) {
        Json action;
        try {
            action = Json::parse(action_json);
        } catch (const std::exception& error) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::InvalidAction,
                    .message = std::string("Invalid Luau playtest action: ") +
                               error.what(),
                }
            );
        }
        return call_without_result(
            module,
            &Module::begin_step,
            world,
            &action,
            PlaytestErrorKind::InvalidAction,
            "begin_step"
        );
    }

    Status<PlaytestError> end_step(std::size_t module, World& world) {
        return call_without_result(
            module,
            &Module::end_step,
            world,
            nullptr,
            PlaytestErrorKind::Internal,
            "end_step"
        );
    }

    Result<std::string, PlaytestError>
    observe(std::size_t module_id, World& world) {
        auto* loaded = module(module_id);
        if (loaded == nullptr) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = "Luau playtest module is not loaded",
                }
            );
        }
        if (loaded->observe == 0) {
            return std::string("{}");
        }

        DynamicWorld context("playtest.observe");
        auto prepared = context.prepare(world);
        if (!prepared) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = std::move(prepared.error().message),
                }
            );
        }
        auto* thread = loaded->thread;
        auto& scope = m_impl->borrow_scope;
        const auto token = scope.begin();
        lua_settop(thread, 0);
        lua_getref(thread, loaded->observe);
        fei::detail::push_luau_borrowed_ref(thread, *prepared, scope, token);
        if (lua_pcall(thread, 1, 1, 0) != 0) {
            auto message = luau_error(thread, "Failed to observe playtest");
            scope.end(token);
            context.finish();
            lua_settop(thread, 0);
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = "observe failed: " + message,
                }
            );
        }
        try {
            auto observation = luau_json(thread, -1, "observation").dump();
            scope.end(token);
            context.finish();
            lua_settop(thread, 0);
            return observation;
        } catch (const std::exception& error) {
            scope.end(token);
            context.finish();
            lua_settop(thread, 0);
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = error.what(),
                }
            );
        }
    }
};

} // namespace

TypeId luau_playtest_runtime_resource_type() {
    return type_id<LuauPlaytestRuntime>();
}

void LuauPlaytestsPlugin::setup(App& app) {
    if (!app.has_resource<runtime_protocol::PlaytestRegistry>()) {
        throw std::runtime_error(
            "LuauPlaytestsPlugin requires a PlaytestRegistry resource"
        );
    }

    LuauPlaytestRuntime runtime;
    std::vector<LoadedPlaytestDeclaration> declarations;
    const auto& references = app.resource<Project>().config().playtests;
    declarations.reserve(references.size());
    auto& assets = app.resource<AssetServer>();
    for (const auto& reference : references) {
        auto path = assets.resolve(reference);
        if (!path) {
            throw std::runtime_error(
                "Failed to resolve Luau playtest declaration: " +
                path.error().message
            );
        }
        if (path->path().extension() != ".luau") {
            throw std::runtime_error(
                "Luau playtest declaration must use the .luau extension: " +
                path->as_string()
            );
        }
        auto bytes = assets.read_asset_bytes(*path);
        if (!bytes) {
            throw std::runtime_error(
                "Failed to read Luau playtest declaration '" +
                path->as_string() + "': " + bytes.error().message
            );
        }
        auto loaded = runtime.load(
            ScriptSource {
                .name = path->as_string(),
                .content = std::string(
                    reinterpret_cast<const char*>(bytes->data()),
                    bytes->size()
                ),
            }
        );
        if (!loaded) {
            throw std::runtime_error(loaded.error().message);
        }
        declarations.push_back(std::move(*loaded));
    }

    app.add_resource(std::move(runtime));
    auto& registry = app.resource<runtime_protocol::PlaytestRegistry>();
    for (auto& declaration : declarations) {
        const auto module = declaration.module;
        auto registered = registry.add(
            runtime_protocol::PlaytestInterfaceRegistration {
                .descriptor = std::move(declaration.descriptor),
                .begin_step =
                    [module](World& world, std::string_view action_json) {
                        return world.resource<LuauPlaytestRuntime>()
                            .begin_step(module, world, action_json);
                    },
                .end_step =
                    [module](World& world) {
                        return world.resource<LuauPlaytestRuntime>().end_step(
                            module,
                            world
                        );
                    },
                .observe =
                    [module](World& world) {
                        return world.resource<LuauPlaytestRuntime>().observe(
                            module,
                            world
                        );
                    },
            }
        );
        if (!registered) {
            throw std::runtime_error(
                "Failed to register Luau playtest interface: " +
                registered.error().message
            );
        }
    }
}

} // namespace fei::project_runtime
