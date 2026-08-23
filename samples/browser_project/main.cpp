#include "app/app.hpp"
#include "base/log.hpp"
#include "core/plugin.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics_webgpu_browser/plugin.hpp"
#include "project/project.hpp"
#include "project_runtime/runtime.hpp"
#include "project_scripting_luau/plugin.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "sprite/plugin.hpp"
#include "sprite/renderer.hpp"
#include "ui_rendering/plugin.hpp"
#include "window_browser/input.hpp"

#include <emscripten.h>
#include <string>
#include <utility>

namespace ets::browser_project_sample {
namespace {

struct ProjectStatus {
    bool published {false};
};

struct ProjectPresentation {
    bool published {false};
};

struct ProjectRenderSystems {
    struct Report : SystemSet<Report> {};
};

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
    ResRW<ProjectPresentation> presentation
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
    publish_status("web project presented");
    presentation->published = true;
}

class BrowserProjectHostPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<WebGpuBrowserPlugin>()
            .require<BrowserInputPlugin>()
            .require<CorePlugin>()
            .require<SpritePlugin>()
            .require<ui::rendering::UiRenderingPlugin>();
    }

    void setup(App& app) override {
        app.add_resource(ProjectStatus {})
            .add_systems(Last, report_project_scripts);
        app.sub_app<RenderApp>()
            .add_resource(ProjectPresentation {})
            .configure_sets(
                RenderLast,
                ProjectRenderSystems::Report {}
                    .after<RenderingSystems::Present>()
            )
            .add_systems(
                RenderLast,
                report_project_frame | in_set<ProjectRenderSystems::Report>() |
                    main_thread()
            );
    }
};

} // namespace
} // namespace ets::browser_project_sample

int main() {
    using namespace ets;
    using namespace ets::browser_project_sample;

    publish_status("loading web project");
    auto project = Project::load("/entisium/assets/web-project/project.yaml");
    if (!project) {
        const auto message = "project load failed: " + project.error().message;
        error("{}", message);
        publish_status(message.c_str());
        return 1;
    }

    App app;
    configure_project_runtime(app, std::move(*project));
    app.add_plugin<BrowserProjectHostPlugin>();
    app.run();
    return 0;
}
