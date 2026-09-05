#include "scripting/playtest_segment.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <Luau/Compiler.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace ets {
namespace {

using Json = nlohmann::json;

constexpr std::size_t c_max_json_depth = 32;
constexpr std::size_t c_max_json_nodes = std::size_t {16} * 1024;

struct InterruptBudget {
    uint32 remaining {0};
};

struct MemoryBudget {
    std::size_t used {0};
};

void* allocate_vm_memory(
    void* userdata,
    void* pointer,
    std::size_t old_size,
    std::size_t new_size
) {
    auto& budget = *static_cast<MemoryBudget*>(userdata);
    if (pointer == nullptr) {
        old_size = 0;
    }
    if (new_size == 0) {
        budget.used = old_size <= budget.used ? budget.used - old_size : 0;
        std::free(pointer);
        return nullptr;
    }
    if (new_size > old_size &&
        (budget.used >= LuauPlaytestSegment::maximum_vm_bytes ||
         new_size - old_size >
             LuauPlaytestSegment::maximum_vm_bytes - budget.used)) {
        return nullptr;
    }
    void* result = std::realloc(pointer, new_size);
    if (result != nullptr) {
        budget.used =
            (old_size <= budget.used ? budget.used - old_size : 0) + new_size;
    }
    return result;
}

std::string lua_error(lua_State* state, std::string_view prefix) {
    std::size_t size = 0;
    const char* text = lua_tolstring(state, -1, &size);
    return std::string(prefix) + ": " +
           (text == nullptr ? "unknown Luau error" : std::string(text, size));
}

int absolute_index(lua_State* state, int index) {
    return index > 0 || index <= LUA_REGISTRYINDEX ?
               index :
               lua_gettop(state) + index + 1;
}

Json read_json(
    lua_State* state,
    int index,
    std::size_t depth,
    std::size_t& nodes
) {
    if (depth > c_max_json_depth || ++nodes > c_max_json_nodes) {
        throw std::runtime_error(
            "Segment action exceeds the JSON complexity limit"
        );
    }
    switch (lua_type(state, index)) {
        case LUA_TBOOLEAN:
            return lua_toboolean(state, index) != 0;
        case LUA_TNUMBER: {
            const auto value = lua_tonumber(state, index);
            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    "Segment action contains a non-finite number"
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
                "Segment action must contain only booleans, numbers, strings, "
                "and tables"
            );
    }

    const int table = absolute_index(state, index);
    const auto array_size = static_cast<std::size_t>(lua_objlen(state, table));
    std::size_t entries = 0;
    bool string_keys = false;
    bool number_keys = false;
    lua_pushnil(state);
    while (lua_next(state, table) != 0) {
        ++entries;
        if (lua_type(state, -2) == LUA_TSTRING) {
            string_keys = true;
        } else if (lua_type(state, -2) == LUA_TNUMBER) {
            const auto key = lua_tonumber(state, -2);
            if (std::trunc(key) != key || key < 1.0 ||
                key > static_cast<double>(array_size)) {
                lua_pop(state, 1);
                throw std::runtime_error(
                    "Segment action contains an invalid array key"
                );
            }
            number_keys = true;
        } else {
            lua_pop(state, 1);
            throw std::runtime_error(
                "Segment action contains an unsupported table key"
            );
        }
        lua_pop(state, 1);
    }
    if (array_size > 0) {
        if (string_keys || !number_keys || entries != array_size) {
            throw std::runtime_error(
                "Segment action cannot mix array entries with object fields"
            );
        }
        Json result = Json::array();
        for (std::size_t item = 1; item <= array_size; ++item) {
            lua_rawgeti(state, table, static_cast<int>(item));
            result.push_back(read_json(state, -1, depth + 1, nodes));
            lua_pop(state, 1);
        }
        return result;
    }
    if (number_keys) {
        throw std::runtime_error("Segment action contains a sparse array");
    }
    Json result = Json::object();
    lua_pushnil(state);
    while (lua_next(state, table) != 0) {
        std::size_t key_size = 0;
        const char* key = lua_tolstring(state, -2, &key_size);
        result[std::string(key, key_size)] =
            read_json(state, -1, depth + 1, nodes);
        lua_pop(state, 1);
    }
    return result;
}

Json read_json(lua_State* state, int index) {
    std::size_t nodes = 0;
    return read_json(state, index, 0, nodes);
}

void push_read_only_json(
    lua_State* state,
    const Json& value,
    std::size_t depth,
    std::size_t& nodes
) {
    if (depth > c_max_json_depth || ++nodes > c_max_json_nodes) {
        throw std::runtime_error(
            "Segment observation exceeds the JSON complexity limit"
        );
    }
    if (value.is_null()) {
        lua_pushnil(state);
    } else if (value.is_boolean()) {
        lua_pushboolean(state, value.get<bool>());
    } else if (value.is_number_integer()) {
        lua_pushinteger(state, value.get<lua_Integer>());
    } else if (value.is_number_unsigned()) {
        lua_pushnumber(state, static_cast<double>(value.get<std::uint64_t>()));
    } else if (value.is_number_float()) {
        lua_pushnumber(state, value.get<double>());
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        lua_pushlstring(state, text.data(), text.size());
    } else if (value.is_array()) {
        lua_newtable(state);
        for (std::size_t index = 0; index < value.size(); ++index) {
            push_read_only_json(state, value[index], depth + 1, nodes);
            lua_rawseti(state, -2, static_cast<int>(index + 1));
        }
        lua_setreadonly(state, -1, true);
    } else if (value.is_object()) {
        lua_newtable(state);
        for (const auto& [key, item] : value.items()) {
            push_read_only_json(state, item, depth + 1, nodes);
            lua_setfield(state, -2, key.c_str());
        }
        lua_setreadonly(state, -1, true);
    } else {
        throw std::runtime_error(
            "Segment observation contains unsupported JSON"
        );
    }
}

void push_read_only_json(lua_State* state, const Json& value) {
    std::size_t nodes = 0;
    push_read_only_json(state, value, 0, nodes);
}

void remove_global(lua_State* state, const char* name) {
    lua_pushnil(state);
    lua_setglobal(state, name);
}

} // namespace

struct LuauPlaytestSegment::Impl {
    InterruptBudget budget;
    MemoryBudget memory;
    lua_State* state {nullptr};
    int function_ref {LUA_NOREF};

    ~Impl() {
        if (state != nullptr) {
            lua_close(state);
        }
    }
};

namespace {

void interrupt(lua_State* state, int gc) {
    if (gc >= 0) {
        return;
    }
    auto* budget =
        static_cast<InterruptBudget*>(lua_callbacks(state)->userdata);
    if (budget == nullptr) {
        return;
    }
    if (budget->remaining > 0) {
        --budget->remaining;
    }
    if (budget->remaining == 0) {
        luaL_error(state, "segment invocation exceeded its interrupt budget");
    }
}

} // namespace

LuauPlaytestSegment::LuauPlaytestSegment(std::unique_ptr<Impl> impl) :
    m_impl(std::move(impl)) {}

LuauPlaytestSegment::~LuauPlaytestSegment() = default;

Result<std::shared_ptr<LuauPlaytestSegment>, LuauScriptError>
LuauPlaytestSegment::compile(std::string_view source) {
    if (source.empty()) {
        return failure(LuauScriptError {"Segment source must not be empty"});
    }
    if (source.size() > maximum_source_bytes) {
        return failure(
            LuauScriptError {"Segment source exceeds the 64 KiB limit"}
        );
    }

    auto impl = std::make_unique<Impl>();
    impl->state = lua_newstate(allocate_vm_memory, &impl->memory);
    if (impl->state == nullptr) {
        return failure(
            LuauScriptError {"Failed to create isolated segment VM"}
        );
    }
    luaL_openlibs(impl->state);
    for (const char* name : {
             "collectgarbage",
             "dofile",
             "getfenv",
             "loadfile",
             "loadstring",
             "newproxy",
             "require",
             "setfenv",
         }) {
        remove_global(impl->state, name);
    }
    luaL_sandbox(impl->state);
    lua_callbacks(impl->state)->userdata = &impl->budget;
    lua_callbacks(impl->state)->interrupt = interrupt;

    std::string bytecode;
    try {
        bytecode = Luau::compile(std::string(source));
    } catch (const std::exception& error) {
        return failure(
            LuauScriptError {
                "Failed to compile segment: " + std::string(error.what()),
            }
        );
    }
    if (luau_load(
            impl->state,
            "=playtest-segment",
            bytecode.data(),
            bytecode.size(),
            0
        ) != 0) {
        return failure(
            LuauScriptError {
                lua_error(impl->state, "Failed to load segment"),
            }
        );
    }
    impl->budget.remaining = invocation_interrupt_budget;
    if (lua_pcall(impl->state, 0, 1, 0) != 0) {
        return failure(
            LuauScriptError {
                lua_error(impl->state, "Failed to initialize segment"),
            }
        );
    }
    if (!lua_isfunction(impl->state, -1)) {
        return failure(
            LuauScriptError {
                "Segment source must return a function accepting ctx",
            }
        );
    }
    impl->function_ref = lua_ref(impl->state, -1);
    lua_pop(impl->state, 1);
    return std::shared_ptr<LuauPlaytestSegment>(
        new LuauPlaytestSegment(std::move(impl))
    );
}

Result<LuauPlaytestSegmentDecision, LuauScriptError>
LuauPlaytestSegment::next(std::string_view observation_json, uint32 tick) {
    auto* state = m_impl->state;
    if (observation_json.size() > maximum_observation_bytes) {
        return failure(
            LuauScriptError {"Segment observation exceeds the 1 MiB limit"}
        );
    }
    try {
        const auto observation = Json::parse(observation_json);
        lua_getref(state, m_impl->function_ref);
        lua_newtable(state);
        lua_pushinteger(state, static_cast<lua_Integer>(tick));
        lua_setfield(state, -2, "tick");
        push_read_only_json(state, observation);
        lua_setfield(state, -2, "observation");
        lua_setreadonly(state, -1, true);
    } catch (const std::exception& error) {
        lua_settop(state, 0);
        return failure(
            LuauScriptError {
                "Failed to prepare segment context: " +
                    std::string(error.what()),
            }
        );
    }

    m_impl->budget.remaining = invocation_interrupt_budget;
    if (lua_pcall(state, 1, 1, 0) != 0) {
        auto message = lua_error(state, "Segment invocation failed");
        lua_settop(state, 0);
        return failure(LuauScriptError {std::move(message)});
    }
    if (!lua_istable(state, -1)) {
        lua_settop(state, 0);
        return failure(
            LuauScriptError {
                "Segment function must return { action = {...} } or { stop = "
                "\"reason\" }",
            }
        );
    }

    const int decision_table = absolute_index(state, -1);
    lua_pushnil(state);
    while (lua_next(state, decision_table) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
            lua_settop(state, 0);
            return failure(
                LuauScriptError {"Segment decision contains a non-string field"}
            );
        }
        std::size_t key_size = 0;
        const char* key = lua_tolstring(state, -2, &key_size);
        const std::string_view field(key, key_size);
        if (field != "action" && field != "stop") {
            lua_settop(state, 0);
            return failure(
                LuauScriptError {
                    "Segment decision contains unsupported field '" +
                        std::string(field) + "'",
                }
            );
        }
        lua_pop(state, 1);
    }

    lua_getfield(state, -1, "action");
    const bool has_action = !lua_isnil(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, -1, "stop");
    const bool has_stop = !lua_isnil(state, -1);
    lua_pop(state, 1);
    if (has_action == has_stop) {
        lua_settop(state, 0);
        return failure(
            LuauScriptError {
                "Segment decision must contain exactly one of 'action' or "
                "'stop'",
            }
        );
    }

    if (has_stop) {
        lua_getfield(state, -1, "stop");
        if (!lua_isstring(state, -1)) {
            lua_settop(state, 0);
            return failure(
                LuauScriptError {"Segment stop reason must be a string"}
            );
        }
        std::size_t size = 0;
        const char* text = lua_tolstring(state, -1, &size);
        std::string reason(text, size);
        lua_settop(state, 0);
        if (reason.empty() || reason.size() > 256) {
            return failure(
                LuauScriptError {
                    "Segment stop reason must contain between 1 and 256 bytes",
                }
            );
        }
        return LuauPlaytestSegmentDecision {
            .kind = LuauPlaytestSegmentDecisionKind::Stop,
            .value = std::move(reason),
        };
    }

    try {
        lua_getfield(state, -1, "action");
        auto action = read_json(state, -1).dump();
        lua_settop(state, 0);
        return LuauPlaytestSegmentDecision {
            .kind = LuauPlaytestSegmentDecisionKind::Action,
            .value = std::move(action),
        };
    } catch (const std::exception& error) {
        lua_settop(state, 0);
        return failure(LuauScriptError {error.what()});
    }
}

} // namespace ets
