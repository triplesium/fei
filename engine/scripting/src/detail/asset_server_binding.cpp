#include "scripting/detail/asset_server_binding.hpp"

#include "asset/server.hpp"
#include "scripting/detail/binding.hpp"

#include <array>
#include <lua.h>
#include <lualib.h>
#include <string>
#include <string_view>

namespace ets::detail {
namespace {

int raise_message(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
    return 0;
}

AssetServer&
check_asset_server_mut(lua_State* state, int index, std::string_view context) {
    auto borrowed = check_luau_borrowed_ref(state, index);
    if (borrowed.ref.is_const()) {
        luaL_error(
            state,
            "%.*s requires ResRW<AssetServer>",
            static_cast<int>(context.size()),
            context.data()
        );
    }
    auto* server = borrowed.ref.try_get<AssetServer>();
    if (server == nullptr) {
        luaL_error(state, "AssetServer method called with invalid receiver");
    }
    return *server;
}

AssetPath check_asset_path(lua_State* state, int index) {
    std::size_t size = 0;
    const char* path = luaL_checklstring(state, index, &size);
    return AssetPath(std::string(path, size));
}

int push_loaded_handle(
    lua_State* state,
    AssetServer& server,
    Result<UntypedHandle, AssetTypeError> loaded
) {
    if (!loaded) {
        return raise_message(state, loaded.error().message);
    }
    auto value = server.handle_value(*loaded);
    if (!value) {
        return raise_message(state, value.error().message);
    }
    push_luau_owned_value(state, std::move(*value));
    return 1;
}

int asset_server_load(lua_State* state) {
    if (lua_gettop(state) != 3) {
        luaL_error(state, "AssetServer.load expects an asset type and path");
    }
    auto& server = check_asset_server_mut(state, 1, "AssetServer.load");
    const TypeId type = check_luau_type_token(state, 2, "AssetServer.load");
    return push_loaded_handle(
        state,
        server,
        server.load(type, check_asset_path(state, 3))
    );
}

int asset_server_load_async(lua_State* state) {
    if (lua_gettop(state) != 3) {
        luaL_error(
            state,
            "AssetServer.load_async expects an asset type and path"
        );
    }
    auto& server = check_asset_server_mut(state, 1, "AssetServer.load_async");
    const TypeId type =
        check_luau_type_token(state, 2, "AssetServer.load_async");
    return push_loaded_handle(
        state,
        server,
        server.load_async(type, check_asset_path(state, 3))
    );
}

} // namespace

bool luau_is_asset_server(TypeId type) {
    return type == type_id<AssetServer>();
}

bool push_luau_asset_server_member(lua_State* state, const char* key) {
    const std::string_view name {key};
    static const std::array methods {
        std::pair<std::string_view, lua_CFunction> {"load", asset_server_load},
        std::pair<std::string_view, lua_CFunction> {
            "load_async",
            asset_server_load_async,
        },
    };
    for (const auto& [method_name, function] : methods) {
        if (name == method_name) {
            lua_pushcfunction(state, function, key);
            return true;
        }
    }
    return false;
}

} // namespace ets::detail
