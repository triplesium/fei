#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "runtime_inspection_ecs/entity.hpp"
#include "runtime_inspection_ecs/query.hpp"
#include "runtime_inspection_ecs/world_summary.hpp"
#include "runtime_protocol/probe.hpp"

#include <chrono>
#include <thread>

namespace {

fei::Result<std::string, fei::runtime_protocol::RuntimeInspectionError>
inspect_runtime(
    fei::World& world,
    const fei::runtime_protocol::InspectionRequest& request
) {
    if (!world.has_resource<fei::runtime_inspection::InspectionRegistry>()) {
        return fei::failure(
            fei::runtime_protocol::RuntimeInspectionError {
                .kind = "internal",
                .message = "Runtime inspection registry is not installed",
            }
        );
    }
    auto response =
        world.resource<fei::runtime_inspection::InspectionRegistry>().dispatch(
            world,
            fei::runtime_inspection::InspectionInvocation {
                .provider = request.provider,
                .schema = request.schema,
                .payload_json = request.payload_json,
            }
        );
    if (!response) {
        return fei::failure(
            fei::runtime_protocol::RuntimeInspectionError {
                .kind = std::string(
                    fei::runtime_inspection::inspection_error_kind_name(
                        response.error().kind
                    )
                ),
                .message = std::move(response.error().message),
            }
        );
    }
    return std::move(*response);
}

} // namespace

int main() {
    fei::App app;
    app.world().entity();
    fei::runtime_inspection::InspectionRegistry inspection_registry;
    auto registration =
        fei::runtime_inspection::ecs::register_entity_inspection_provider(
            inspection_registry
        );
    if (!registration) {
        return 1;
    }
    registration =
        fei::runtime_inspection::ecs::register_query_inspection_provider(
            inspection_registry
        );
    if (!registration) {
        return 1;
    }
    registration = fei::runtime_inspection::ecs::
        register_world_summary_inspection_provider(inspection_registry);
    if (!registration) {
        return 1;
    }
    inspection_registry.freeze();
    fei::runtime_protocol::RuntimeProbeConfig runtime_probe_config {
        .project = "runtime-probe-fixture",
        .project_file = "memory://runtime-probe-fixture",
        .build_id = "test-build",
        .heartbeat_interval_ms = 100,
        .inspection_handler = inspect_runtime,
    };
    runtime_probe_config.inspections.reserve(
        inspection_registry.descriptors().size()
    );
    for (const auto& descriptor : inspection_registry.descriptors()) {
        runtime_probe_config.inspections.push_back(
            fei::runtime_protocol::InspectionCapability {
                .id = descriptor.id,
                .label = descriptor.label,
                .description = descriptor.description,
                .schema = descriptor.schema,
                .read_only = descriptor.read_only,
                .cost = std::string(
                    fei::runtime_inspection::inspection_cost_name(
                        descriptor.cost
                    )
                ),
                .request_schema_json = descriptor.request_schema_json,
                .response_schema_json = descriptor.response_schema_json,
            }
        );
    }
    app.add_resource(std::move(inspection_registry));
    app.add_plugin<fei::ReflectionPlugin>();
    app.add_plugin(
        fei::runtime_protocol::RuntimeProbePlugin {
            std::move(runtime_probe_config),
        }
    );
    app.startup();
    for (int frame = 0; frame < 1500; ++frame) {
        app.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    app.shutdown();
    return 0;
}
