#pragma once

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "base/log.hpp"
#include "base/result.hpp"
#include "devtools/bridge.hpp"
#include "devtools/json.hpp"
#include "devtools/types.hpp"
#include "ecs/commands.hpp"
#include "ecs/fwd.hpp"

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fei {

class World;

namespace devtools {

Entity declare_capability(
    World& world,
    std::string id,
    std::string label,
    BlobProtocol protocol
);

Entity declare_capability(
    World& world,
    std::string id,
    std::string label,
    JsonProtocol protocol
);

template<class T>
concept CapabilityDefinition = requires {
    typename T::RequestBody;
    typename T::ResponseBody;
    { T::id } -> std::convertible_to<std::string_view>;
    { T::label } -> std::convertible_to<std::string_view>;
    { T::schema } -> std::convertible_to<std::string_view>;
};

template<class T>
concept ExecutableCapability = CapabilityDefinition<T> && requires(App& app) {
    app.add_systems(T::schedule, T::run);
};

template<CapabilityDefinition Definition>
Entity declare_capability(World& world) {
    JsonProtocol protocol {.schema = std::string(Definition::schema)};
    if constexpr (!std::is_void_v<typename Definition::RequestBody>) {
        protocol.request_type = type_id<typename Definition::RequestBody>();
    }
    if constexpr (!std::is_void_v<typename Definition::ResponseBody>) {
        protocol.response_type = type_id<typename Definition::ResponseBody>();
    }
    return declare_capability(
        world,
        std::string(Definition::id),
        std::string(Definition::label),
        std::move(protocol)
    );
}

template<ExecutableCapability Definition>
Entity add_capability(App& app) {
    if (!app.has_resource<Bridge>()) {
        fatal(
            "DevTools capability '{}' requires devtools::CorePlugin. Add "
            "devtools::CorePlugin before its provider.",
            Definition::id
        );
    }

    if constexpr (
        !std::is_void_v<typename Definition::RequestBody> ||
        !std::is_void_v<typename Definition::ResponseBody>
    ) {
        if (!app.has_plugin<ReflectionPlugin>()) {
            fatal(
                "DevTools capability '{}' requires ReflectionPlugin. Add "
                "ReflectionPlugin before its provider.",
                Definition::id
            );
        }
    }

    auto entity = declare_capability<Definition>(app.world());
    app.add_systems(Definition::schedule, Definition::run);
    return entity;
}

template<ExecutableCapability... Definitions>
void add_capabilities(App& app) {
    (add_capability<Definitions>(app), ...);
}

inline void respond_capability_error(
    Commands& commands,
    Entity entity,
    const Request& request,
    int status,
    std::string message
) {
    commands.entity(entity).add(
        ErrorResponse {
            .token = request.token,
            .capability = request.capability,
            .status = status,
            .message = std::move(message),
        }
    );
}

template<class Response>
void respond_capability(
    Commands& commands,
    Entity entity,
    const Request& request,
    const Response& response
) {
    auto json = encode_json(Ref(response));
    if (!json) {
        respond_capability_error(
            commands,
            entity,
            request,
            500,
            std::move(json.error())
        );
        return;
    }
    commands.entity(entity).add(
        JsonResponse {
            .token = request.token,
            .capability = request.capability,
            .json = std::move(*json),
        }
    );
}

template<class RequestBody>
Result<RequestBody, std::string>
decode_capability_request(const JsonRequest& request) {
    if (!request.body) {
        return failure(std::string {"Capability requires a request body"});
    }
    return decode_json<RequestBody>(*request.body);
}

} // namespace devtools

} // namespace fei
