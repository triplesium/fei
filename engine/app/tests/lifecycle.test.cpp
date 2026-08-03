#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/system_params.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace fei;

namespace {

struct LifecycleTrace {
    std::vector<std::string> entries;
};

void record_pre_startup(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("pre-startup");
}

void record_startup(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("startup");
}

void record_first(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("first");
}

void record_update(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("update");
}

void record_last(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("last");
}

void record_render_prepare(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("render-prepare");
}

void record_render_update(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("render-update");
}

void record_render_last(ResRW<LifecycleTrace> trace) {
    trace->entries.emplace_back("render-last");
}

class LifecyclePlugin : public Plugin {
  public:
    static inline std::vector<std::string> plugin_entries;

    void setup(App& app) override {
        plugin_entries.emplace_back("setup");
        app.add_resource(LifecycleTrace {})
            .add_systems(PreStartUp, record_pre_startup)
            .add_systems(StartUp, record_startup)
            .add_systems(First, record_first)
            .add_systems(Update, record_update)
            .add_systems(Last, record_last)
            .add_systems(RenderPrepare, record_render_prepare)
            .add_systems(RenderUpdate, record_render_update)
            .add_systems(RenderLast, record_render_last);
    }

    void finish(App& /*app*/) override {
        plugin_entries.emplace_back("finish");
    }

    void cleanup(App& /*app*/) noexcept override {
        plugin_entries.emplace_back("cleanup");
    }
};

} // namespace

TEST_CASE(
    "App lifecycle can be driven manually and is idempotent",
    "[app][lifecycle]"
) {
    LifecyclePlugin::plugin_entries.clear();

    App app;
    app.add_plugin<LifecyclePlugin>();
    REQUIRE(app.lifecycle() == AppLifecycle::Building);

    app.finish();
    app.finish();
    REQUIRE(app.lifecycle() == AppLifecycle::Ready);
    REQUIRE(
        LifecyclePlugin::plugin_entries ==
        std::vector<std::string> {"setup", "finish"}
    );

    app.startup();
    app.startup();
    REQUIRE(app.lifecycle() == AppLifecycle::Running);

    app.update();
    app.render();
    REQUIRE(
        app.resource<LifecycleTrace>().entries == std::vector<std::string> {
                                                      "pre-startup",
                                                      "startup",
                                                      "first",
                                                      "update",
                                                      "last",
                                                      "render-prepare",
                                                      "render-update",
                                                      "render-last",
                                                  }
    );

    app.shutdown();
    app.shutdown();
    REQUIRE(app.lifecycle() == AppLifecycle::Stopped);
    REQUIRE(
        LifecyclePlugin::plugin_entries ==
        std::vector<std::string> {"setup", "finish", "cleanup"}
    );

    app.update();
    app.render();
    app.startup();
    REQUIRE(
        app.resource<LifecycleTrace>().entries == std::vector<std::string> {
                                                      "pre-startup",
                                                      "startup",
                                                      "first",
                                                      "update",
                                                      "last",
                                                      "render-prepare",
                                                      "render-update",
                                                      "render-last",
                                                  }
    );
}

TEST_CASE("App update starts a building app", "[app][lifecycle]") {
    LifecyclePlugin::plugin_entries.clear();

    App app;
    app.add_plugin<LifecyclePlugin>();
    app.update();

    REQUIRE(app.lifecycle() == AppLifecycle::Running);
    REQUIRE(
        LifecyclePlugin::plugin_entries ==
        std::vector<std::string> {"setup", "finish"}
    );
    REQUIRE(
        app.resource<LifecycleTrace>().entries == std::vector<std::string> {
                                                      "pre-startup",
                                                      "startup",
                                                      "first",
                                                      "update",
                                                      "last",
                                                  }
    );

    app.shutdown();
}
