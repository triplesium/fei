#include "devtools_ecs/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "runtime_inspection_ecs/entity.hpp"
#include "runtime_inspection_ecs/query.hpp"
#include "runtime_inspection_ecs/world_summary.hpp"

#include <string_view>
#include <utility>

namespace ets::devtools::ecs {
namespace {

namespace inspection_ecs = runtime_inspection::ecs;

int inspection_error_status(runtime_inspection::InspectionErrorKind kind) {
    switch (kind) {
        case runtime_inspection::InspectionErrorKind::InvalidRequest:
            return 400;
        case runtime_inspection::InspectionErrorKind::NotFound:
            return 404;
        case runtime_inspection::InspectionErrorKind::Conflict:
            return 409;
        case runtime_inspection::InspectionErrorKind::Unsupported:
            return 422;
        case runtime_inspection::InspectionErrorKind::ResponseTooLarge:
            return 413;
        case runtime_inspection::InspectionErrorKind::Internal:
            return 500;
    }
    return 500;
}

struct EcsQuery {
    using RequestBody = inspection_ecs::QueryRequest;
    using ResponseBody = void;

    static constexpr std::string_view id {
        inspection_ecs::QueryInspectionProvider::id
    };
    static constexpr std::string_view label {
        inspection_ecs::QueryInspectionProvider::label
    };
    static constexpr std::string_view schema {
        inspection_ecs::QueryInspectionProvider::schema
    };
    static constexpr ScheduleId schedule {PostUpdate};

    static void
    run(WorldRef world,
        Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            if (!json.body) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    400,
                    "Capability requires a request body"
                );
                continue;
            }
            auto response =
                world->resource<runtime_inspection::InspectionRegistry>()
                    .dispatch(
                        *world,
                        runtime_inspection::InspectionInvocation {
                            .provider = id,
                            .schema = schema,
                            .payload_json = *json.body,
                        }
                    );
            if (!response) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    inspection_error_status(response.error().kind),
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
    using RequestBody = inspection_ecs::EntityInspectRequest;
    using ResponseBody = void;

    static constexpr std::string_view id {"ecs.entity.inspect"};
    static constexpr std::string_view label {"Inspect ECS Entity"};
    static constexpr std::string_view schema {
        inspection_ecs::EntityInspectionProvider::schema
    };
    static constexpr ScheduleId schedule {PostUpdate};

    static void
    run(WorldRef world,
        Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            if (!json.body) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    400,
                    "Capability requires a request body"
                );
                continue;
            }
            auto response =
                world->resource<runtime_inspection::InspectionRegistry>()
                    .dispatch(
                        *world,
                        runtime_inspection::InspectionInvocation {
                            .provider = id,
                            .schema = schema,
                            .payload_json = *json.body,
                        }
                    );
            if (!response) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    inspection_error_status(response.error().kind),
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

struct EcsWorldSummary {
    using RequestBody = inspection_ecs::WorldSummaryRequest;
    using ResponseBody = void;

    static constexpr std::string_view id {
        inspection_ecs::WorldSummaryInspectionProvider::id
    };
    static constexpr std::string_view label {
        inspection_ecs::WorldSummaryInspectionProvider::label
    };
    static constexpr std::string_view schema {
        inspection_ecs::WorldSummaryInspectionProvider::schema
    };
    static constexpr ScheduleId schedule {PostUpdate};

    static void
    run(WorldRef world,
        Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            if (request.capability != id) {
                continue;
            }

            if (!json.body) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    400,
                    "Capability requires a request body"
                );
                continue;
            }
            auto response =
                world->resource<runtime_inspection::InspectionRegistry>()
                    .dispatch(
                        *world,
                        runtime_inspection::InspectionInvocation {
                            .provider = id,
                            .schema = schema,
                            .payload_json = *json.body,
                        }
                    );
            if (!response) {
                respond_capability_error(
                    commands,
                    entity,
                    request,
                    inspection_error_status(response.error().kind),
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
    if (!app.has_resource<runtime_inspection::InspectionRegistry>()) {
        app.add_resource(runtime_inspection::InspectionRegistry {});
    }
    auto& registry = app.resource<runtime_inspection::InspectionRegistry>();
    if (!registry.contains(inspection_ecs::QueryInspectionProvider::id)) {
        auto status =
            inspection_ecs::register_query_inspection_provider(registry);
        if (!status) {
            fatal(
                "Failed to register ECS query inspection provider: {}",
                status.error().message
            );
        }
    }
    if (!registry.contains(inspection_ecs::EntityInspectionProvider::id)) {
        auto status =
            inspection_ecs::register_entity_inspection_provider(registry);
        if (!status) {
            fatal(
                "Failed to register ECS inspection provider: {}",
                status.error().message
            );
        }
    }
    if (!registry.contains(
            inspection_ecs::WorldSummaryInspectionProvider::id
        )) {
        auto status =
            inspection_ecs::register_world_summary_inspection_provider(
                registry
            );
        if (!status) {
            fatal(
                "Failed to register ECS world summary inspection provider: "
                "{}",
                status.error().message
            );
        }
    }
    add_capability<EcsQuery>(app);
    add_capability<EcsEntityInspect>(app);
    add_capability<EcsWorldSummary>(app);
}

void ProviderPlugin::finish(App& app) {
    app.resource<runtime_inspection::InspectionRegistry>().freeze();
}

} // namespace ets::devtools::ecs
