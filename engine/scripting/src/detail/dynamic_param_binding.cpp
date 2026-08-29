#include "binding_internal.hpp"
#include "ecs/dynamic/events.hpp"
#include "ecs/dynamic/removed_components.hpp"
#include "ecs/dynamic/state.hpp"

#include <lua.h>
#include <lualib.h>
#include <string>
#include <string_view>
#include <utility>

namespace ets::detail {
namespace {

int raise_message(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
    return 0;
}

int dynamic_state_get(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    const auto* dynamic_state = borrowed.ref.try_get_const<DynamicStateRef>();
    if (dynamic_state == nullptr || borrowed.scope == nullptr) {
        return raise_message(state, "State.get called with invalid receiver");
    }
    Ref value = dynamic_state->get();
    if (!value) {
        return raise_message(state, "State value is not initialized");
    }
    push_luau_borrowed_value(state, value, *borrowed.scope, borrowed.token);
    return 1;
}

int dynamic_next_state_set(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    const auto* next_state = borrowed.ref.try_get_const<DynamicNextStateRef>();
    if (next_state == nullptr) {
        return raise_message(
            state,
            "NextState.set called with invalid receiver"
        );
    }
    auto value = copy_luau_reflected_value(state, 2, "NextState.set");
    if (!value) {
        return raise_message(state, value.error());
    }
    auto status = next_state->set(value->ref());
    if (!status) {
        return raise_message(state, status.error().message);
    }
    lua_pushvalue(state, 1);
    return 1;
}

int dynamic_next_state_clear(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    const auto* next_state = borrowed.ref.try_get_const<DynamicNextStateRef>();
    if (next_state == nullptr) {
        return raise_message(
            state,
            "NextState.clear called with invalid receiver"
        );
    }
    next_state->clear();
    lua_pushvalue(state, 1);
    return 1;
}

bool push_dynamic_state_member(
    lua_State* state,
    TypeId type,
    std::string_view key
) {
    if (type == type_id<DynamicStateRef>() && key == "get") {
        lua_pushcfunction(state, dynamic_state_get, "State.get");
        return true;
    }
    if (type != type_id<DynamicNextStateRef>()) {
        return false;
    }
    if (key == "set") {
        lua_pushcfunction(state, dynamic_next_state_set, "NextState.set");
        return true;
    }
    if (key == "clear" || key == "reset") {
        lua_pushcfunction(state, dynamic_next_state_clear, "NextState.clear");
        return true;
    }
    return false;
}

int dynamic_removed_next(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* removed = borrowed.ref.try_get<DynamicRemovedComponents>();
    if (removed == nullptr) {
        return raise_message(state, "RemovedComponents.next invalid receiver");
    }
    auto entity = removed->next();
    if (!entity) {
        return 0;
    }
    lua_pushunsigned(state, entity->value);
    return 1;
}

int dynamic_removed_clear(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* removed = borrowed.ref.try_get<DynamicRemovedComponents>();
    if (removed == nullptr) {
        return raise_message(state, "RemovedComponents.clear invalid receiver");
    }
    removed->clear();
    return 0;
}

bool push_dynamic_removed_member(
    lua_State* state,
    TypeId type,
    std::string_view key
) {
    if (type != type_id<DynamicRemovedComponents>()) {
        return false;
    }
    if (key == "next" || key == "removed") {
        lua_pushcfunction(
            state,
            dynamic_removed_next,
            "RemovedComponents.next"
        );
        return true;
    }
    if (key == "clear") {
        lua_pushcfunction(
            state,
            dynamic_removed_clear,
            "RemovedComponents.clear"
        );
        return true;
    }
    return false;
}

int dynamic_event_send(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* writer = borrowed.ref.try_get<DynamicEventParam>();
    if (writer == nullptr || writer->kind() != DynamicEventParamKind::Writer) {
        return raise_message(state, "EventWriter.send invalid receiver");
    }
    auto payload = copy_luau_reflected_value(state, 2, "EventWriter.send");
    if (!payload) {
        return raise_message(state, payload.error());
    }
    auto sent = writer->send(std::move(*payload));
    if (!sent) {
        return raise_message(state, sent.error().message);
    }
    return 0;
}

int dynamic_event_next(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* reader = borrowed.ref.try_get<DynamicEventParam>();
    if (reader == nullptr || reader->kind() == DynamicEventParamKind::Writer) {
        return raise_message(state, "EventReader.next invalid receiver");
    }
    auto event = reader->next();
    if (!event) {
        return 0;
    }
    push_luau_borrowed_value(state, *event, *borrowed.scope, borrowed.token);
    return 1;
}

int dynamic_event_reset(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* reader = borrowed.ref.try_get<DynamicEventParam>();
    if (reader == nullptr || reader->kind() == DynamicEventParamKind::Writer) {
        return raise_message(state, "EventReader.reset invalid receiver");
    }
    reader->reset();
    return 0;
}

bool push_dynamic_event_member(
    lua_State* state,
    TypeId type,
    std::string_view key
) {
    if (type != type_id<DynamicEventParam>()) {
        return false;
    }
    if (key == "send") {
        lua_pushcfunction(state, dynamic_event_send, "EventWriter.send");
        return true;
    }
    if (key == "next") {
        lua_pushcfunction(state, dynamic_event_next, "EventReader.next");
        return true;
    }
    if (key == "reset") {
        lua_pushcfunction(state, dynamic_event_reset, "EventReader.reset");
        return true;
    }
    return false;
}

} // namespace

bool push_luau_dynamic_param_member(
    lua_State* state,
    TypeId type,
    std::string_view key
) {
    return push_dynamic_state_member(state, type, key) ||
           push_dynamic_removed_member(state, type, key) ||
           push_dynamic_event_member(state, type, key);
}

bool is_luau_dynamic_param_callable(Ref receiver) {
    const auto* event = receiver.try_get_const<DynamicEventParam>();
    return event != nullptr && event->kind() == DynamicEventParamKind::Writer;
}

int invoke_luau_dynamic_param(lua_State* state) {
    return dynamic_event_send(state);
}

} // namespace ets::detail
