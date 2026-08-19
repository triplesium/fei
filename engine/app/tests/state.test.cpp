#include "ecs/state.hpp"

#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::app_test;

namespace {

enum class AppLifecycleState {
    Loading,
    Playing,
};

struct AppStateTrace {
    std::vector<std::string> entries;
};

void request_playing(ResRW<NextState<AppLifecycleState>> next_state) {
    next_state->set(AppLifecycleState::Playing);
}

void update_when_playing(ResRW<AppStateTrace> trace) {
    trace->entries.emplace_back("update:playing");
}

void enter_loading(ResRW<AppStateTrace> trace) {
    trace->entries.emplace_back("enter:loading");
}

void record_pre_startup(ResRW<AppStateTrace> trace) {
    trace->entries.emplace_back("pre-startup");
}

} // namespace

TEST_CASE("App enters the initial state before PreStartUp", "[app][state]") {
    StopOnFinishPlugin::setup_count = 0;
    StopOnFinishPlugin::finish_count = 0;

    App app;
    app.add_plugin<StopOnFinishPlugin>()
        .init_state(AppLifecycleState::Loading)
        .add_resource(AppStateTrace {})
        .add_systems(OnEnter(AppLifecycleState::Loading), enter_loading)
        .add_systems(PreStartUp, record_pre_startup);

    app.run();

    REQUIRE(
        app.resource<AppStateTrace>().entries ==
        std::vector<std::string> {"enter:loading", "pre-startup"}
    );
}

TEST_CASE(
    "App applies state transitions between PreUpdate and Update",
    "[app][state]"
) {
    StopOnFinishPlugin::setup_count = 0;
    StopOnFinishPlugin::finish_count = 0;

    App app;
    app.add_plugin<StopOnFinishPlugin>()
        .init_state(AppLifecycleState::Loading)
        .add_resource(AppStateTrace {})
        .add_systems(PreUpdate, request_playing)
        .add_systems(
            Update,
            update_when_playing | run_if(in_state(AppLifecycleState::Playing))
        );

    app.run();

    REQUIRE(
        app.resource<AppStateTrace>().entries ==
        std::vector<std::string> {"update:playing"}
    );
    REQUIRE(
        app.resource<State<AppLifecycleState>>().get() ==
        AppLifecycleState::Playing
    );
    REQUIRE_FALSE(app.resource<NextState<AppLifecycleState>>().has_value());
}
