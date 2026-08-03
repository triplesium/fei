#include "devtools_ecs/plugin.hpp"

#include "app/app.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "entity_inspect.hpp"
#include "query.hpp"

#include <string_view>
#include <utility>

namespace fei::devtools::ecs {
namespace {

struct EcsQuery {
    using RequestBody = QueryRequest;
    using ResponseBody = void;

    static constexpr std::string_view id {"ecs.query"};
    static constexpr std::string_view label {"Query ECS Entities"};
    static constexpr std::string_view schema {"ecs.query.v1"};
    static constexpr ScheduleId schedule {PostUpdate};

    static void
    run(WorldRef world,
        Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            auto body = decode_capability_request<RequestBody>(json);
            if (!body) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    400,
                    std::move(body.error())
                );
                continue;
            }

            auto response = execute_query(*world, *body);
            if (!response) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    response.error().status,
                    std::move(response.error().message)
                );
                continue;
            }
            commands.entity(entity).add(
                JsonResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .json = std::move(*response),
                }
            );
        }
    }
};

struct EcsEntityInspect {
    using RequestBody = EntityInspectRequest;
    using ResponseBody = void;

    static constexpr std::string_view id {"ecs.entity.inspect"};
    static constexpr std::string_view label {"Inspect ECS Entity"};
    static constexpr std::string_view schema {"ecs.entity.inspect.v1"};
    static constexpr ScheduleId schedule {PostUpdate};

    static void
    run(WorldRef world,
        Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            auto body = decode_capability_request<RequestBody>(json);
            if (!body) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    400,
                    std::move(body.error())
                );
                continue;
            }

            auto response = inspect_entity(*world, *body);
            if (!response) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    response.error().status,
                    std::move(response.error().message)
                );
                continue;
            }
            commands.entity(entity).add(
                JsonResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .json = std::move(*response),
                }
            );
        }
    }
};

} // namespace

void ProviderPlugin::setup(App& app) {
    add_capability<EcsQuery>(app);
    add_capability<EcsEntityInspect>(app);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::ecs
