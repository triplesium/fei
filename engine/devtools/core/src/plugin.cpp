#include "devtools/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "devtools/schema.hpp"
#include "devtools/server.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei::devtools {

namespace {

struct SchemaSyncState {
    std::vector<TypeId> roots;
    bool initialized {false};
};

std::string reflected_type_name(TypeId id) {
    if (!id) {
        return {};
    }
    auto type = Registry::instance().try_get_type(id);
    return type ? type->name() : std::string {};
}

void append_schema_root(std::vector<TypeId>& roots, TypeId id) {
    if (id) {
        roots.push_back(id);
    }
}

void import_devtools_requests(
    ResRW<Bridge> bridge,
    Commands commands,
    Query<Entity, const Subscription> subscriptions
) {
    for (const auto& change : bridge->take_subscription_changes()) {
        if (change.start) {
            commands.spawn().add(
                Subscription {
                    .token = change.token,
                    .capability = change.capability,
                }
            );
            continue;
        }

        for (auto [entity, subscription] : subscriptions) {
            if (subscription.token == change.token) {
                commands.entity(entity).despawn();
            }
        }
    }

    for (const auto& request : bridge->take_pending_requests()) {
        auto entity = commands.spawn();
        entity.add(
            Request {
                .token = request.token,
                .capability = request.capability,
                .deadline = request.deadline,
                .fresh = request.fresh,
            }
        );

        switch (request.protocol) {
            case ProtocolKind::Blob:
                entity.add(BlobRequest {.after = request.after});
                break;
            case ProtocolKind::Json:
                entity.add(JsonRequest {.body = request.body});
                break;
        }
    }
}

void sync_devtools_manifest(
    ResRW<Bridge> bridge,
    ResRW<SchemaSyncState> schema_state,
    Query<const Capability, const BlobProtocol> blobs,
    Query<const Capability, const JsonProtocol> json_capabilities
) {
    std::vector<ManifestEntry> entries;
    std::vector<TypeId> schema_roots;
    for (auto [capability, blob] : blobs) {
        entries.push_back(
            ManifestEntry {
                .id = capability.id,
                .label = capability.label,
                .mime = blob.mime,
                .mode = blob.mode,
                .waitable = blob.waitable,
                .endpoints = {
                    ManifestEndpoint {
                        .rel = "read",
                        .method = "GET",
                        .path = "/api/v1/capabilities/" + capability.id,
                        .params = {"after", "fresh", "timeout_ms"},
                    },
                    ManifestEndpoint {
                        .rel = "stream",
                        .method = "GET",
                        .path =
                            "/api/v1/capabilities/" + capability.id + "/stream",
                        .params = {"after"},
                    },
                },
            }
        );
    }
    for (auto [capability, protocol] : json_capabilities) {
        entries.push_back(
            ManifestEntry {
                .id = capability.id,
                .label = capability.label,
                .schema = protocol.schema,
                .request_type = protocol.request_type ?
                                    Optional<std::string> {reflected_type_name(
                                        *protocol.request_type
                                    )} :
                                    nullopt,
                .response_type = protocol.response_type ?
                                     Optional<std::string> {reflected_type_name(
                                         *protocol.response_type
                                     )} :
                                     nullopt,
                .mode = PublishMode::OnDemand,
                .waitable = true,
                .endpoints = {
                    ManifestEndpoint {
                        .rel = "invoke",
                        .method = "POST",
                        .path = "/api/v1/capabilities/" + capability.id,
                        .params = {"timeout_ms"},
                    },
                },
            }
        );
        if (protocol.request_type) {
            append_schema_root(schema_roots, *protocol.request_type);
        }
        if (protocol.response_type) {
            append_schema_root(schema_roots, *protocol.response_type);
        }
    }
    bridge->update_manifest(std::move(entries));

    std::ranges::sort(schema_roots);
    schema_roots.erase(
        std::ranges::unique(schema_roots).begin(),
        schema_roots.end()
    );
    if (schema_state->initialized && schema_state->roots == schema_roots) {
        return;
    }

    schema_state->roots = schema_roots;
    schema_state->initialized = true;
    auto schema = build_schema_json(schema_roots);
    if (!schema) {
        error("Failed to build DevTools schemas: {}", schema.error());
        return;
    }
    bridge->update_schema_json(std::move(*schema));
}

bool has_response_component(World& world, Entity entity) {
    return world.has_component<BlobResponse>(entity) ||
           world.has_component<JsonResponse>(entity) ||
           world.has_component<ErrorResponse>(entity);
}

void expire_devtools_requests(
    Query<Entity, const Request> requests,
    Commands commands
) {
    const auto now = std::chrono::steady_clock::now();
    auto& world = commands.world();
    for (auto [entity, request] : requests) {
        if (request.deadline <= now && !has_response_component(world, entity)) {
            commands.entity(entity).add(
                ErrorResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .status = 504,
                    .message = "DevTools request expired",
                }
            );
        }
    }
}

void export_devtools_blob_responses(
    ResRW<Bridge> bridge,
    Query<Entity, BlobResponse> responses,
    Commands commands
) {
    auto& world = commands.world();
    for (auto [entity, response] : responses) {
        auto out = std::move(response.write());
        if (world.has_component<Request>(entity)) {
            const auto& request = world.get_component<Request>(entity);
            if (out.token == 0) {
                out.token = request.token;
            }
            if (out.capability.empty()) {
                out.capability = request.capability;
            }
        }
        bridge->publish_blob(std::move(out));
        commands.entity(entity).despawn();
    }
}

void export_devtools_json_responses(
    ResRW<Bridge> bridge,
    Query<Entity, JsonResponse> responses,
    Commands commands
) {
    auto& world = commands.world();
    for (auto [entity, response] : responses) {
        auto out = std::move(response.write());
        if (world.has_component<Request>(entity)) {
            const auto& request = world.get_component<Request>(entity);
            if (out.token == 0) {
                out.token = request.token;
            }
            if (out.capability.empty()) {
                out.capability = request.capability;
            }
        }
        bridge->complete_response(std::move(out));
        commands.entity(entity).despawn();
    }
}

void export_devtools_error_responses(
    ResRW<Bridge> bridge,
    Query<Entity, ErrorResponse> responses,
    Commands commands
) {
    auto& world = commands.world();
    for (auto [entity, response] : responses) {
        auto out = std::move(response.write());
        if (world.has_component<Request>(entity)) {
            const auto& request = world.get_component<Request>(entity);
            if (out.token == 0) {
                out.token = request.token;
            }
            if (out.capability.empty()) {
                out.capability = request.capability;
            }
        }
        bridge->complete_error(std::move(out));
        commands.entity(entity).despawn();
    }
}

struct Shutdown {
    using RequestBody = void;
    using ResponseBody = void;

    static constexpr std::string_view id {"devtools.shutdown"};
    static constexpr std::string_view label {"Shutdown"};
    static constexpr std::string_view schema {"devtools.shutdown.v1"};
    static constexpr ScheduleId schedule {Update};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        ResRW<AppStates> app_states,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            app_states->should_stop = true;
            commands.entity(entity).add(
                JsonResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .json = R"({"ok":true})",
                }
            );
        }
    }
};

} // namespace

CorePlugin::CorePlugin(Config config) : m_config(std::move(config)) {}

void CorePlugin::setup(App& app) {
    app.add_resource(Bridge {});
    app.add_resource(SchemaSyncState {});

    add_capability<Shutdown>(app);

    auto bridge = app.resource<Bridge>();
    app.add_resource(Server(m_config, bridge));
    app.resource<Server>().start();

    app.add_systems(First, import_devtools_requests);
    app.add_systems(RenderFirst, import_devtools_requests);
    app.add_systems(RenderEnd, Shutdown::run);
    app.add_systems(
        Last,
        sync_devtools_manifest,
        expire_devtools_requests,
        export_devtools_blob_responses,
        export_devtools_json_responses,
        export_devtools_error_responses
    );
    app.add_systems(
        RenderLast,
        sync_devtools_manifest,
        expire_devtools_requests,
        export_devtools_blob_responses,
        export_devtools_json_responses,
        export_devtools_error_responses
    );
}

void CorePlugin::finish(App&) {}

} // namespace fei::devtools
