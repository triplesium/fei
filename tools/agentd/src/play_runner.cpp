#include "play_runner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace fei::agentd {
namespace {

using Json = nlohmann::json;

constexpr std::size_t c_max_json_depth = 32;
constexpr std::size_t c_max_json_nodes = std::size_t {16} * 1024;

struct MemoryBudget {
    std::size_t used {0};
    std::size_t peak {0};
    std::size_t maximum {0};
};

void* limited_allocator(
    void* user_data,
    void* pointer,
    std::size_t previous_size,
    std::size_t new_size
) {
    auto& budget = *static_cast<MemoryBudget*>(user_data);
    if (new_size == 0) {
        std::free(pointer);
        budget.used -= std::min(budget.used, previous_size);
        return nullptr;
    }

    const auto released = std::min(budget.used, previous_size);
    const auto retained = budget.used - released;
    if (new_size > budget.maximum - std::min(budget.maximum, retained)) {
        return nullptr;
    }
    void* result = std::realloc(pointer, new_size);
    if (result == nullptr) {
        return nullptr;
    }
    budget.used = retained + new_size;
    budget.peak = std::max(budget.peak, budget.used);
    return result;
}

int absolute_index(lua_State* state, int index) {
    return index > 0 || index <= LUA_REGISTRYINDEX ?
               index :
               lua_gettop(state) + index + 1;
}

Json luau_json(
    lua_State* state,
    int index,
    std::string_view context,
    std::size_t depth,
    std::size_t& nodes
) {
    if (depth > c_max_json_depth || ++nodes > c_max_json_nodes) {
        throw std::runtime_error(
            std::string(context) + " exceeds the JSON complexity limit"
        );
    }

    switch (lua_type(state, index)) {
        case LUA_TNIL:
            return nullptr;
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
                " must contain only nil, booleans, numbers, strings, and "
                "tables"
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
    if (depth > c_max_json_depth) {
        throw std::runtime_error("Play response exceeds the JSON depth limit");
    }
    if (value.is_null()) {
        lua_pushnil(state);
        return;
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
    throw std::runtime_error("Play response contains unsupported JSON");
}

struct RunContext {
    const PlayControlBindings& bindings;
    const PlayRunLimits& limits;
    const PlayRunObserver& observer;
    std::chrono::steady_clock::time_point deadline;
    Json calls = Json::array();
    Json logs = Json::array();
    std::unordered_map<std::string, uint32> decision_ticks;
    std::size_t call_count {0};
    uint64 ticks {0};
    std::size_t interrupts {0};
};

template<typename Callback, typename... Arguments>
void notify_observer(const Callback& callback, Arguments&&... arguments) {
    if (!callback) {
        return;
    }
    try {
        callback(std::forward<Arguments>(arguments)...);
    } catch (...) {
        // Observability must never alter playtest behavior.
        return;
    }
}

RunContext& context(lua_State* state) {
    return *static_cast<RunContext*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
}

void check_deadline(const RunContext& run) {
    if (std::chrono::steady_clock::now() > run.deadline) {
        throw std::runtime_error("play-run exceeded its time limit");
    }
}

template<typename Invoke>
Json invoke_play(
    RunContext& run,
    std::string operation,
    Json request,
    Invoke&& invoke
) {
    check_deadline(run);
    if (run.call_count >= run.limits.maximum_calls) {
        throw std::runtime_error("play-run exceeded its call limit");
    }
    const auto call_index = ++run.call_count;
    const auto started_at = std::chrono::steady_clock::now();
    Json trace {
        {"index", call_index},
        {"operation", operation},
        {"request", std::move(request)},
    };
    notify_observer(
        run.observer.call_started,
        call_index,
        std::string_view(operation),
        std::as_const(trace.at("request"))
    );
    Result<Json, std::string> response;
    try {
        response = invoke();
    } catch (const std::exception& error) {
        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at
            );
        trace["duration_ms"] = duration.count();
        trace["ok"] = false;
        trace["error"] = error.what();
        run.calls.push_back(std::move(trace));
        notify_observer(run.observer.call_finished, call_index);
        throw std::runtime_error(operation + " failed: " + error.what());
    }
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    );
    trace["duration_ms"] = duration.count();
    if (!response) {
        trace["ok"] = false;
        trace["error"] = response.error();
        run.calls.push_back(std::move(trace));
        notify_observer(run.observer.call_finished, call_index);
        throw std::runtime_error(operation + " failed: " + response.error());
    }
    trace["ok"] = true;
    trace["response"] = *response;
    run.calls.push_back(std::move(trace));
    notify_observer(run.observer.call_finished, call_index);
    check_deadline(run);
    return std::move(*response);
}

void cache_interfaces(RunContext& run, const Json& response) {
    if (!response.is_object() || !response.contains("interfaces") ||
        !response.at("interfaces").is_array()) {
        throw std::runtime_error("play.interfaces returned invalid data");
    }
    for (const auto& interface : response.at("interfaces")) {
        if (interface.is_object() && interface.contains("id") &&
            interface.contains("decision_ticks") &&
            interface.at("id").is_string() &&
            interface.at("decision_ticks").is_number_integer()) {
            run.decision_ticks[interface.at("id").get<std::string>()] =
                interface.at("decision_ticks").get<uint32>();
        }
    }
}

Json fetch_interfaces(RunContext& run) {
    auto response = invoke_play(run, "interfaces", Json::object(), [&run]() {
        if (!run.bindings.interfaces) {
            return Result<Json, std::string>(
                failure(std::string("interfaces binding is unavailable"))
            );
        }
        return run.bindings.interfaces();
    });
    cache_interfaces(run, response);
    return response;
}

uint32 resolve_step_ticks(
    RunContext& run,
    std::string_view interface_id,
    std::optional<uint32> requested
) {
    if (requested) {
        return *requested;
    }
    auto ticks = run.decision_ticks.find(std::string(interface_id));
    if (ticks == run.decision_ticks.end()) {
        (void)fetch_interfaces(run);
        ticks = run.decision_ticks.find(std::string(interface_id));
    }
    if (ticks == run.decision_ticks.end()) {
        throw std::runtime_error(
            "Unknown playtest interface '" + std::string(interface_id) + "'"
        );
    }
    return ticks->second;
}

std::string string_argument(lua_State* state, int index, const char* name) {
    if (!lua_isstring(state, index)) {
        throw std::runtime_error(std::string(name) + " must be a string");
    }
    std::size_t size = 0;
    const char* value = lua_tolstring(state, index, &size);
    return std::string(value, size);
}

std::optional<uint32> optional_ticks(lua_State* state, int index) {
    if (lua_gettop(state) < index || lua_isnil(state, index)) {
        return std::nullopt;
    }
    if (!lua_isnumber(state, index)) {
        throw std::runtime_error("ticks must be a positive integer");
    }
    const auto value = lua_tonumber(state, index);
    if (std::trunc(value) != value || value < 1.0 ||
        value > static_cast<double>(std::numeric_limits<uint32>::max())) {
        throw std::runtime_error("ticks must be a positive integer");
    }
    return static_cast<uint32>(value);
}

int play_interfaces(lua_State* state) {
    try {
        if (lua_gettop(state) != 0) {
            throw std::runtime_error("play.interfaces expects no arguments");
        }
        auto response = fetch_interfaces(context(state));
        push_json(state, response.at("interfaces"));
        return 1;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

int play_step(lua_State* state) {
    try {
        const auto argument_count = lua_gettop(state);
        if (argument_count < 2 || argument_count > 3) {
            throw std::runtime_error(
                "play.step expects interface, action, and optional ticks"
            );
        }
        auto interface_id = string_argument(state, 1, "interface");
        if (!lua_istable(state, 2)) {
            throw std::runtime_error("action must be a table");
        }
        auto action = luau_json(state, 2, "action");
        auto requested_ticks = optional_ticks(state, 3);
        auto& run = context(state);
        const auto ticks =
            resolve_step_ticks(run, interface_id, requested_ticks);
        if (ticks > run.limits.maximum_ticks -
                        std::min(run.limits.maximum_ticks, run.ticks)) {
            throw std::runtime_error("play-run exceeded its tick limit");
        }
        Json request {
            {"interface", interface_id},
            {"action", action},
        };
        if (requested_ticks) {
            request["ticks"] = *requested_ticks;
        }
        auto response = invoke_play(
            run,
            "step",
            std::move(request),
            [&run, &interface_id, &action, requested_ticks]() {
                if (!run.bindings.step) {
                    return Result<Json, std::string>(
                        failure(std::string("step binding is unavailable"))
                    );
                }
                return run.bindings.step(interface_id, action, requested_ticks);
            }
        );
        const auto completed_ticks = response.at("ticks").get<uint64>();
        if (completed_ticks >
            run.limits.maximum_ticks -
                std::min(run.limits.maximum_ticks, run.ticks)) {
            throw std::runtime_error("play-run exceeded its tick limit");
        }
        run.ticks += completed_ticks;
        push_json(state, response);
        return 1;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

int play_observe(lua_State* state) {
    try {
        if (lua_gettop(state) != 1) {
            throw std::runtime_error("play.observe expects one interface");
        }
        auto interface_id = string_argument(state, 1, "interface");
        auto& run = context(state);
        auto response = invoke_play(
            run,
            "observe",
            Json {{"interface", interface_id}},
            [&run, &interface_id]() {
                if (!run.bindings.observe) {
                    return Result<Json, std::string>(
                        failure(std::string("observe binding is unavailable"))
                    );
                }
                return run.bindings.observe(interface_id);
            }
        );
        push_json(state, response.at("observation"));
        return 1;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

int play_capture(lua_State* state) {
    try {
        if (lua_gettop(state) > 1) {
            throw std::runtime_error(
                "play.capture expects an optional output path"
            );
        }
        std::optional<std::string> output;
        if (lua_gettop(state) == 1 && !lua_isnil(state, 1)) {
            output = string_argument(state, 1, "output path");
        }
        auto& run = context(state);
        auto response = invoke_play(
            run,
            "capture",
            output ? Json {{"output", *output}} : Json::object(),
            [&run, &output]() {
                if (!run.bindings.capture) {
                    return Result<Json, std::string>(
                        failure(std::string("capture binding is unavailable"))
                    );
                }
                return run.bindings.capture(output);
            }
        );
        push_json(state, response);
        return 1;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

int play_checkpoint(lua_State* state) {
    try {
        const auto argument_count = lua_gettop(state);
        if (argument_count < 1 || argument_count > 2) {
            throw std::runtime_error(
                "play.checkpoint expects a name and optional strict flag"
            );
        }
        auto name = string_argument(state, 1, "checkpoint name");
        bool strict = false;
        if (argument_count == 2) {
            if (!lua_isboolean(state, 2)) {
                throw std::runtime_error("strict must be a boolean");
            }
            strict = lua_toboolean(state, 2) != 0;
        }
        auto& run = context(state);
        auto response = invoke_play(
            run,
            "checkpoint",
            Json {{"name", name}, {"strict", strict}},
            [&run, &name, strict]() {
                if (!run.bindings.checkpoint) {
                    return Result<Json, std::string>(failure(
                        std::string("checkpoint binding is unavailable")
                    ));
                }
                return run.bindings.checkpoint(name, strict);
            }
        );
        push_json(state, response);
        return 1;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

int play_restore(lua_State* state) {
    try {
        if (lua_gettop(state) != 1) {
            throw std::runtime_error(
                "play.restore expects one checkpoint name"
            );
        }
        auto name = string_argument(state, 1, "checkpoint name");
        auto& run = context(state);
        auto response =
            invoke_play(run, "restore", Json {{"name", name}}, [&run, &name]() {
                if (!run.bindings.restore) {
                    return Result<Json, std::string>(
                        failure(std::string("restore binding is unavailable"))
                    );
                }
                return run.bindings.restore(name);
            });
        push_json(state, response);
        return 1;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

int play_log(lua_State* state) {
    try {
        if (lua_gettop(state) != 1) {
            throw std::runtime_error("play.log expects one value");
        }
        auto& run = context(state);
        auto value = luau_json(state, 1, "log value");
        run.logs.push_back(value);
        notify_observer(run.observer.log, std::as_const(value));
        return 0;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

int play_print(lua_State* state) {
    try {
        Json values = Json::array();
        for (int index = 1; index <= lua_gettop(state); ++index) {
            values.push_back(luau_json(state, index, "print value"));
        }
        auto value =
            values.size() == 1 ? std::move(values.front()) : std::move(values);
        auto& run = context(state);
        run.logs.push_back(value);
        notify_observer(run.observer.log, std::as_const(value));
        return 0;
    } catch (const std::exception& error) {
        luaL_error(state, "%s", error.what());
        return 0;
    }
}

void interrupt_script(lua_State* state, int gc) {
    if (gc >= 0) {
        return;
    }
    auto* run = static_cast<RunContext*>(lua_callbacks(state)->userdata);
    if (run == nullptr) {
        return;
    }
    ++run->interrupts;
    if (run->interrupts <= run->limits.maximum_interrupts &&
        std::chrono::steady_clock::now() <= run->deadline) {
        return;
    }
    lua_callbacks(state)->interrupt = nullptr;
    lua_rawcheckstack(state, 1);
    luaL_error(state, "play-run execution budget exceeded");
}

void push_bound_function(
    lua_State* state,
    RunContext& run,
    lua_CFunction function,
    const char* name
) {
    lua_pushlightuserdata(state, &run);
    lua_pushcclosure(state, function, name, 1);
}

void remove_global(lua_State* state, const char* name) {
    lua_pushnil(state);
    lua_setglobal(state, name);
}

void remove_unsafe_globals(lua_State* state) {
    for (const char* name : {
             "debug",
             "dofile",
             "io",
             "loadfile",
             "loadstring",
             "os",
             "package",
             "require",
         }) {
        remove_global(state, name);
    }
}

void install_play_api(lua_State* state, RunContext& run) {
    lua_newtable(state);
    for (const auto& [name, function] : {
             std::pair<const char*, lua_CFunction> {
                 "interfaces",
                 play_interfaces,
             },
             {"step", play_step},
             {"observe", play_observe},
             {"capture", play_capture},
             {"checkpoint", play_checkpoint},
             {"restore", play_restore},
             {"log", play_log},
         }) {
        push_bound_function(state, run, function, name);
        lua_setfield(state, -2, name);
    }
    lua_setreadonly(state, -1, true);
    lua_setglobal(state, "play");

    push_bound_function(state, run, play_print, "print");
    lua_setglobal(state, "print");
}

std::string script_error(lua_State* state, std::string fallback) {
    const char* message = lua_tostring(state, -1);
    std::string error =
        message != nullptr ? std::string(message) : std::move(fallback);
    if (const char* trace = lua_debugtrace(state);
        trace != nullptr && *trace != '\0') {
        error += "\n";
        error += trace;
    }
    return error;
}

Json report(
    bool ok,
    Json result,
    std::string error,
    const RunContext& run,
    const MemoryBudget& memory
) {
    return Json {
        {"ok", ok},
        {"result", ok ? std::move(result) : Json(nullptr)},
        {"error", ok ? Json(nullptr) : Json(std::move(error))},
        {"calls", run.calls},
        {"logs", run.logs},
        {"budget",
         Json {
             {"calls", run.call_count},
             {"ticks", run.ticks},
             {"interrupts", run.interrupts},
             {"peak_memory_bytes", memory.peak},
         }},
    };
}

} // namespace

Json run_luau_play_script(
    std::string_view source,
    const PlayControlBindings& bindings,
    const PlayRunLimits& limits,
    const PlayRunObserver& observer
) {
    MemoryBudget memory {.maximum = limits.maximum_memory_bytes};
    RunContext run {
        .bindings = bindings,
        .limits = limits,
        .observer = observer,
        .deadline = std::chrono::steady_clock::now() + limits.maximum_duration,
    };
    if (source.size() > limits.maximum_source_bytes) {
        return report(
            false,
            nullptr,
            "play-run source exceeds its size limit",
            run,
            memory
        );
    }
    if (source.empty()) {
        return report(
            false,
            nullptr,
            "play-run source must not be empty",
            run,
            memory
        );
    }

    lua_State* state = lua_newstate(limited_allocator, &memory);
    if (state == nullptr) {
        return report(
            false,
            nullptr,
            "Failed to create the play-run Luau VM",
            run,
            memory
        );
    }
    luaL_openlibs(state);
    remove_unsafe_globals(state);
    luaL_sandbox(state);
    luaL_sandboxthread(state);
    install_play_api(state, run);
    lua_callbacks(state)->userdata = &run;
    lua_callbacks(state)->interrupt = interrupt_script;

    std::size_t bytecode_size = 0;
    char* bytecode =
        luau_compile(source.data(), source.size(), nullptr, &bytecode_size);
    if (bytecode == nullptr) {
        auto result =
            report(false, nullptr, "Luau compilation failed", run, memory);
        lua_close(state);
        return result;
    }
    const int load_status =
        luau_load(state, "=play-run", bytecode, bytecode_size, 0);
    std::free(bytecode);
    if (load_status != 0) {
        auto error = script_error(state, "Failed to load Luau bytecode");
        auto result = report(false, nullptr, std::move(error), run, memory);
        lua_close(state);
        return result;
    }

    if (lua_pcall(state, 0, LUA_MULTRET, 0) != 0) {
        auto error = script_error(state, "Failed to run Luau play script");
        auto result = report(false, nullptr, std::move(error), run, memory);
        lua_close(state);
        return result;
    }
    if (lua_gettop(state) > 1) {
        auto result = report(
            false,
            nullptr,
            "play-run script must return at most one value",
            run,
            memory
        );
        lua_close(state);
        return result;
    }

    Json value = nullptr;
    try {
        if (lua_gettop(state) == 1) {
            value = luau_json(state, -1, "script result");
        }
    } catch (const std::exception& error) {
        auto result = report(false, nullptr, error.what(), run, memory);
        lua_close(state);
        return result;
    }
    auto result = report(true, std::move(value), {}, run, memory);
    lua_close(state);
    return result;
}

} // namespace fei::agentd
