#include "scripting_lua/detail/script_decl.hpp"
#include "scripting_lua/detail/utils.hpp"
#include "scripting_lua/runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <lua.hpp>
#include <string>
#include <string_view>

namespace fei {
namespace {

constexpr std::uint64_t c_hook_interval = 1'000;
char c_eval_context_registry_key = 0;
constexpr std::size_t c_max_output_entries = 1'024;

struct EvalContext {
    LuaEvalResult result;
    std::size_t max_output_bytes {0};
    std::size_t output_bytes {0};
    std::uint64_t remaining_instructions {0};
    std::uint64_t hook_interval {1};
    std::chrono::steady_clock::time_point deadline;
};

bool blocked_eval_global(std::string_view name) {
    constexpr std::array blocked {
        std::string_view {"_G"},
        std::string_view {"collectgarbage"},
        std::string_view {"coroutine"},
        std::string_view {"debug"},
        std::string_view {"dofile"},
        std::string_view {"io"},
        std::string_view {"load"},
        std::string_view {"loadfile"},
        std::string_view {"os"},
        std::string_view {"package"},
        std::string_view {"pcall"},
        std::string_view {"require"},
        std::string_view {"xpcall"},
    };
    return std::ranges::find(blocked, name) != blocked.end();
}

int eval_global_index(lua_State* L) {
    std::size_t length = 0;
    const auto* key = lua_tolstring(L, 2, &length);
    if (key && blocked_eval_global(std::string_view {key, length})) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushglobaltable(L);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    lua_remove(L, -2);
    return 1;
}

void append_output(
    EvalContext& context,
    std::string& line,
    const char* text,
    std::size_t size
) {
    if (size == 0) {
        return;
    }
    const auto remaining = context.max_output_bytes > context.output_bytes ?
                               context.max_output_bytes - context.output_bytes :
                               0;
    const auto appended = std::min(remaining, size);
    line.append(text, appended);
    context.output_bytes += appended;
    if (appended != size) {
        context.result.truncated = true;
    }
}

int capture_print(lua_State* L) {
    auto* context =
        static_cast<EvalContext*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!context) {
        return luaL_error(L, "DevTools print capture is unavailable");
    }
    if (context->output_bytes >= context->max_output_bytes) {
        context->result.truncated = true;
        return 0;
    }
    if (context->result.output.size() >= c_max_output_entries) {
        context->result.truncated = true;
        return 0;
    }

    std::string line;
    const auto count = lua_gettop(L);
    for (int index = 1; index <= count; ++index) {
        if (index != 1) {
            append_output(*context, line, "\t", 1);
        }
        std::size_t length = 0;
        const auto* text = luaL_tolstring(L, index, &length);
        append_output(*context, line, text, length);
        lua_pop(L, 1);
    }
    context->result.output.push_back(std::move(line));
    return 0;
}

EvalContext* eval_context(lua_State* L) {
    lua_pushlightuserdata(L, &c_eval_context_registry_key);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* context = static_cast<EvalContext*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return context;
}

void eval_instruction_hook(lua_State* L, lua_Debug*) {
    auto* context = eval_context(L);
    if (!context) {
        luaL_error(L, "DevTools Lua execution context is unavailable");
        return;
    }

    if (std::chrono::steady_clock::now() >= context->deadline) {
        luaL_error(L, "DevTools Lua execution time limit exceeded");
        return;
    }
    if (context->remaining_instructions <= context->hook_interval) {
        luaL_error(L, "DevTools Lua instruction limit exceeded");
        return;
    }
    context->remaining_instructions -= context->hook_interval;
}

void set_eval_context(lua_State* L, EvalContext* context) {
    lua_pushlightuserdata(L, &c_eval_context_registry_key);
    if (context) {
        lua_pushlightuserdata(L, context);
    } else {
        lua_pushnil(L);
    }
    lua_rawset(L, LUA_REGISTRYINDEX);
}

std::string lua_error_message(lua_State* L) {
    std::size_t length = 0;
    const auto* message = lua_tolstring(L, -1, &length);
    if (!message) {
        return "Lua execution failed with a non-string error";
    }
    return std::string {message, length};
}

} // namespace

LuaEvalResult LuaRuntime::eval_script(
    const LuaScriptSource& source,
    std::span<const LuaEvalGlobal> globals,
    const LuaEvalOptions& options
) {
    auto* L = m_state;
    if (eval_context(L)) {
        return LuaEvalResult {
            .error = "Nested Lua evaluation is not supported",
        };
    }
    const auto base_top = lua_gettop(L);
    EvalContext context {
        .max_output_bytes = options.max_output_bytes,
        .remaining_instructions = options.instruction_limit,
        .hook_interval = std::clamp<std::uint64_t>(
            options.instruction_limit,
            1,
            c_hook_interval
        ),
        .deadline = std::chrono::steady_clock::now() + options.time_limit,
    };

    if (luaL_loadbuffer(
            L,
            source.content.data(),
            source.content.size(),
            source.name.c_str()
        ) != LUA_OK) {
        context.result.error = lua_error_message(L);
        lua_settop(L, base_top);
        return std::move(context.result);
    }

    const auto chunk_index = lua_gettop(L);
    lua_newtable(L);
    const auto env_index = lua_gettop(L);

    lua_newtable(L);
    lua_pushcfunction(L, &eval_global_index);
    lua_setfield(L, -2, "__index");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, env_index);

    auto helpers = install_lua_script_helpers(L, env_index);
    if (!helpers) {
        context.result.error = std::move(helpers.error().message);
        lua_settop(L, base_top);
        return std::move(context.result);
    }

    for (const auto& global : globals) {
        lua_push_ref(L, global.value);
        lua_setfield(L, env_index, std::string(global.name).c_str());
    }
    lua_pushlightuserdata(L, &context);
    lua_pushcclosure(L, &capture_print, 1);
    lua_setfield(L, env_index, "print");

    lua_pushvalue(L, env_index);
    const auto* upvalue = lua_setupvalue(L, chunk_index, 1);
    if (!upvalue) {
        lua_pop(L, 1);
        context.result.error = "Lua chunk does not expose an environment";
        lua_settop(L, base_top);
        return std::move(context.result);
    }
    lua_remove(L, env_index);

    set_eval_context(L, &context);
    lua_sethook(
        L,
        &eval_instruction_hook,
        LUA_MASKCOUNT,
        static_cast<int>(context.hook_interval)
    );
    const auto status = lua_pcall(L, 0, 0, 0);
    lua_sethook(L, nullptr, 0, 0);
    set_eval_context(L, nullptr);

    if (status != LUA_OK) {
        context.result.error = lua_error_message(L);
    } else {
        context.result.ok = true;
    }
    lua_settop(L, base_top);
    return std::move(context.result);
}

} // namespace fei
