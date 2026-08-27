#include "app/app.hpp"
#include "base/log.hpp"
#include "core/plugin.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics_webgpu_browser/plugin.hpp"
#include "project/project.hpp"
#include "project_clock.hpp"
#include "project_runtime/runtime.hpp"
#include "project_scripting_luau/playtest.hpp"
#include "project_scripting_luau/plugin.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "runtime_inspection_playtest/playtest.hpp"
#include "runtime_inspection_profiling/profiling.hpp"
#include "sprite/plugin.hpp"
#include "sprite/renderer.hpp"
#include "ui_rendering/plugin.hpp"
#include "window_browser/input.hpp"

#include <emscripten.h>
#include <string>
#include <string_view>
#include <utility>

namespace {

ets::World* g_runtime_world = nullptr;

void publish_inspection(
    const char* request_id,
    bool ok,
    std::string_view value,
    std::string_view error_kind,
    std::string_view error_message
) {
    const std::string value_text(value);
    const std::string error_kind_text(error_kind);
    const std::string error_message_text(error_message);
    EM_ASM(
        {
            window.entisiumPublishInspection(
                UTF8ToString($0),
                Boolean($1),
                UTF8ToString($2),
                UTF8ToString($3),
                UTF8ToString($4),
            );
        },
        request_id,
        ok,
        value_text.c_str(),
        error_kind_text.c_str(),
        error_message_text.c_str()
    );
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void entisium_inspect_runtime(
    const char* request_id,
    const char* provider,
    const char* schema,
    const char* payload_json
) {
    if (g_runtime_world == nullptr ||
        !g_runtime_world
             ->has_resource<ets::runtime_inspection::InspectionRegistry>()) {
        publish_inspection(
            request_id,
            false,
            {},
            "internal",
            "Runtime inspection is not initialized"
        );
        return;
    }
    auto response =
        g_runtime_world->resource<ets::runtime_inspection::InspectionRegistry>()
            .dispatch(
                *g_runtime_world,
                ets::runtime_inspection::InspectionInvocation {
                    .provider = provider,
                    .schema = schema,
                    .payload_json = payload_json,
                }
            );
    if (!response) {
        publish_inspection(
            request_id,
            false,
            {},
            ets::runtime_inspection::inspection_error_kind_name(
                response.error().kind
            ),
            response.error().message
        );
        return;
    }
    publish_inspection(request_id, true, *response, {}, {});
}

namespace ets::editor_runtime {
namespace {

struct ProjectStatus {
    bool published {false};
};

struct ProjectPresentation {
    bool published {false};
};

struct ProjectRenderSystems {
    struct Capture : SystemSet<Capture> {};
    struct Report : SystemSet<Report> {};
};

void publish_runtime_world(WorldRef world) {
    g_runtime_world = world.operator->();
}

bool capture_requested() {
    return EM_ASM_INT({ return window.entisiumCaptureRequested ?.() ? 1 : 0; }) != 0;
}

void capture_project_frame() {
    if (!capture_requested()) {
        return;
    }
    EM_ASM({ window.entisiumCaptureFrame(); });
}

void publish_status(const char* status) {
    EM_ASM(
        {
            const status = UTF8ToString($0);
            document.documentElement.dataset.entisiumProjectStatus = status;
            console.log("[entisium] " + status);
        },
        status
    );
}

void report_project_scripts(
    ResRO<project_runtime::LuauScriptsState> scripts,
    ResRW<ProjectStatus> status
) {
    if (status->published) {
        return;
    }
    if (scripts->scripts.empty()) {
        publish_status("project has no Luau scripts");
        status->published = true;
        return;
    }
    for (const auto& script : scripts->scripts) {
        if (script.status == project_runtime::LuauScriptStatus::Failed) {
            publish_status(("project script failed: " + script.error).c_str());
            status->published = true;
            return;
        }
        if (script.status != project_runtime::LuauScriptStatus::Loaded) {
            return;
        }
    }
    EM_ASM({ document.documentElement.dataset.entisiumProjectScript = "loaded"; });
    publish_status("project script loaded");
    status->published = true;
}

void report_project_frame(
    ResRO<SpritePhase> phase,
    ResRW<ProjectPresentation> presentation,
    ResRO<ProjectPresentationSignal> signal
) {
    if (presentation->published || !phase->active || phase->batches.empty()) {
        return;
    }
    EM_ASM(
        {
            document.documentElement.dataset.entisiumProjectFramePresented = "true";
            document.documentElement.dataset.entisiumStatus =
                "web project presented";
        }
    );
    signal->present();
    publish_status("web project presented");
    presentation->published = true;
}

void release_project_clock(
    ResRW<ProjectClockGate> gate,
    ResRW<Time> time,
    ResRW<FixedTime> fixed_time
) {
    gate->release_if_presented(*time, *fixed_time);
}

class BrowserProjectHostPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<WebGpuBrowserPlugin>()
            .require<BrowserInputPlugin>()
            .require<CorePlugin>()
            .require<project_runtime::LuauPlaytestsPlugin>()
            .require<SpritePlugin>()
            .require<ui::rendering::UiRenderingPlugin>();
    }

    void setup(App& app) override {
        ProjectPresentationSignal presentation_signal;
        app.add_resource(ProjectStatus {})
            .add_resource(ProjectClockGate(presentation_signal))
            .add_systems(First, publish_runtime_world)
            .add_systems(PreUpdate, release_project_clock)
            .add_systems(Last, report_project_scripts);
        app.sub_app<RenderApp>()
            .add_resource(ProjectPresentation {})
            .add_resource(std::move(presentation_signal))
            .configure_sets(
                RenderLast,
                ProjectRenderSystems::Capture {}
                    .before<RenderingSystems::Present>(),
                ProjectRenderSystems::Report {}
                    .after<RenderingSystems::Present>()
            )
            .add_systems(
                RenderLast,
                capture_project_frame |
                    in_set<ProjectRenderSystems::Capture>() | main_thread(),
                report_project_frame | in_set<ProjectRenderSystems::Report>() |
                    main_thread()
            );
    }

    void finish(App& app) override {
        app.resource<ProjectClockGate>().arm(app.resource<Time>());
    }
};

} // namespace
} // namespace ets::editor_runtime

int main() {
    using namespace ets;
    using namespace ets::editor_runtime;

    publish_status("loading web project");
    auto project = Project::load("/entisium/assets/web-project/project.yaml");
    if (!project) {
        const auto message = "project load failed: " + project.error().message;
        error("{}", message);
        publish_status(message.c_str());
        return 1;
    }

    App app;
    runtime_inspection::InspectionRegistry inspections;
    auto registered =
        runtime_inspection::playtest::register_playtest_inspection_providers(
            inspections
        );
    if (!registered) {
        error(
            "Failed to register playtest inspections: {}",
            registered.error().message
        );
        return 1;
    }
    registered =
        runtime_inspection::profiling::register_profiling_inspection_providers(
            inspections
        );
    if (!registered) {
        error(
            "Failed to register profiling inspections: {}",
            registered.error().message
        );
        return 1;
    }
    inspections.freeze();
    app.add_resource(std::move(inspections));
    configure_project_runtime(app, std::move(*project));
    app.add_plugin<BrowserProjectHostPlugin>();
    app.run();
    return 0;
}
