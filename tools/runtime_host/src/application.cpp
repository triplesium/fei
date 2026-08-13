#include "runtime_host/application.hpp"

#include "base/env.hpp"
#include "base/log.hpp"
#include "project_runtime/runtime.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "runtime_inspection_ecs/entity.hpp"
#include "runtime_inspection_ecs/query.hpp"
#include "runtime_inspection_ecs/world_summary.hpp"
#include "runtime_protocol/probe.hpp"
#include "window/window.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace fei::runtime_host {
namespace {

Result<std::string, runtime_protocol::RuntimeInspectionError> inspect_runtime(
    World& world,
    const runtime_protocol::InspectionRequest& request
) {
    if (!world.has_resource<runtime_inspection::InspectionRegistry>()) {
        return failure(
            runtime_protocol::RuntimeInspectionError {
                .kind = "internal",
                .message = "Runtime inspection registry is not installed",
            }
        );
    }
    auto response =
        world.resource<runtime_inspection::InspectionRegistry>().dispatch(
            world,
            runtime_inspection::InspectionInvocation {
                .provider = request.provider,
                .schema = request.schema,
                .payload_json = request.payload_json,
            }
        );
    if (!response) {
        return failure(
            runtime_protocol::RuntimeInspectionError {
                .kind = std::string(
                    runtime_inspection::inspection_error_kind_name(
                        response.error().kind
                    )
                ),
                .message = std::move(response.error().message),
            }
        );
    }
    return std::move(*response);
}

void validate_project_plugins(const ProjectRuntimeConfig& runtime) {
    for (const auto& plugin : runtime.plugins) {
        if (plugin.qualified_name() == "runtime_protocol::RuntimeProbe") {
            throw std::runtime_error(
                "Plugin 'runtime_protocol::RuntimeProbe' is managed by "
                "Runtime Host and cannot be enabled by a project"
            );
        }
    }
}

} // namespace

RuntimeHostApplication::RuntimeHostApplication(Project project) {
    runtime_protocol::RuntimeProbeConfig runtime_probe_config {
        .project = project.config().name,
        .project_file = project.project_file().generic_string(),
        .inspection_handler = inspect_runtime,
    };
    runtime_inspection::InspectionRegistry inspection_registry;
    auto registration =
        runtime_inspection::ecs::register_entity_inspection_provider(
            inspection_registry
        );
    if (!registration) {
        fatal(
            "Failed to register runtime inspection provider: {}",
            registration.error().message
        );
    }
    registration = runtime_inspection::ecs::register_query_inspection_provider(
        inspection_registry
    );
    if (!registration) {
        fatal(
            "Failed to register runtime inspection provider: {}",
            registration.error().message
        );
    }
    registration =
        runtime_inspection::ecs::register_world_summary_inspection_provider(
            inspection_registry
        );
    if (!registration) {
        fatal(
            "Failed to register runtime inspection provider: {}",
            registration.error().message
        );
    }
    inspection_registry.freeze();
    runtime_probe_config.inspections.reserve(
        inspection_registry.descriptors().size()
    );
    for (const auto& descriptor : inspection_registry.descriptors()) {
        runtime_probe_config.inspections.push_back(
            runtime_protocol::InspectionCapability {
                .id = descriptor.id,
                .label = descriptor.label,
                .description = descriptor.description,
                .schema = descriptor.schema,
                .read_only = descriptor.read_only,
                .cost = std::string(
                    runtime_inspection::inspection_cost_name(descriptor.cost)
                ),
                .request_schema_json = descriptor.request_schema_json,
                .response_schema_json = descriptor.response_schema_json,
            }
        );
    }

    m_app.add_resource(std::move(inspection_registry))
        .add_resource(
            WindowConfig {
                .width = 1600,
                .height = 900,
                .title = "Fei Runtime Host",
            }
        );
    validate_project_plugins(project.config().runtime);
    configure_project_runtime(m_app, std::move(project));
    m_app.add_plugin(
        runtime_protocol::RuntimeProbePlugin {
            std::move(runtime_probe_config),
        }
    );
}

RuntimeHostApplication::~RuntimeHostApplication() {
    shutdown();
}

void RuntimeHostApplication::shutdown() noexcept {
    m_app.shutdown();
}

void RuntimeHostApplication::update_frame() {
    m_app.update();
    m_app.render();
}

void RuntimeHostApplication::run() {
    if (m_app.lifecycle() == AppLifecycle::Stopped) {
        return;
    }

    const auto exit_after_seconds =
        read_environment_variable<double>("FEI_EXIT_AFTER_SECONDS");
    const auto exit_after_frames =
        read_environment_variable<std::uint64_t>("FEI_EXIT_AFTER_FRAMES");
    const auto start_time = std::chrono::steady_clock::now();
    std::uint64_t frame_count = 0;

    try {
        m_app.startup();
        bool should_stop = false;
        while (!should_stop) {
            update_frame();
            ++frame_count;

            auto& app_states = m_app.resource<AppStates>();
            if (exit_after_frames && frame_count >= *exit_after_frames) {
                app_states.should_stop = true;
            }
            if (exit_after_seconds) {
                const auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start_time
                );
                if (elapsed.count() >= *exit_after_seconds) {
                    app_states.should_stop = true;
                }
            }
            should_stop = app_states.should_stop;
        }
    } catch (...) {
        shutdown();
        throw;
    }
    shutdown();
}

} // namespace fei::runtime_host
