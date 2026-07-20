#include "devtools_reflection/plugin.hpp"

#include "app/app.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "reflection_metadata.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace fei::devtools::reflection {
namespace {

void respond_error(
    Commands& commands,
    Entity entity,
    const Request& request,
    ReflectionError error
) {
    respond_capability_error(
        commands,
        entity,
        request,
        error.status,
        std::move(error.message)
    );
}

struct ReflectionSearch {
    using RequestBody = SearchRequest;
    using ResponseBody = SearchResponse;

    static constexpr std::string_view id {"reflection.search"};
    static constexpr std::string_view label {"Search Reflected Types"};
    static constexpr std::string_view schema {"reflection.search.v1"};
    static constexpr ScheduleId schedule {Update};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            auto body = decode_capability_request<RequestBody>(json);
            if (!body) {
                respond_error(
                    commands,
                    entity,
                    request,
                    ReflectionError {400, std::move(body.error())}
                );
                continue;
            }
            auto response = search_types(*body);
            if (!response) {
                respond_error(
                    commands,
                    entity,
                    request,
                    std::move(response.error())
                );
                continue;
            }
            respond_capability(commands, entity, request, *response);
        }
    }
};

struct ReflectionDescribe {
    using RequestBody = DescribeRequest;
    using ResponseBody = TypeDescriptor;

    static constexpr std::string_view id {"reflection.describe"};
    static constexpr std::string_view label {"Describe Reflected Type"};
    static constexpr std::string_view schema {"reflection.describe.v1"};
    static constexpr ScheduleId schedule {Update};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            auto body = decode_capability_request<RequestBody>(json);
            if (!body) {
                respond_error(
                    commands,
                    entity,
                    request,
                    ReflectionError {400, std::move(body.error())}
                );
                continue;
            }
            auto response = describe_type(*body);
            if (!response) {
                respond_error(
                    commands,
                    entity,
                    request,
                    std::move(response.error())
                );
                continue;
            }
            respond_capability(commands, entity, request, *response);
        }
    }
};

} // namespace

void ProviderPlugin::setup(App& app) {
    add_capabilities<ReflectionSearch, ReflectionDescribe>(app);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::reflection
