#include "project_clock.hpp"

#include "app/app.hpp"
#include "core/time.hpp"
#include "ecs/system_params.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;
using namespace ets::editor_runtime;

namespace {

void release_project_clock(
    ResRW<ProjectClockGate> gate,
    ResRW<Time> time,
    ResRW<FixedTime> fixed_time
) {
    gate->release_if_presented(*time, *fixed_time);
}

} // namespace

TEST_CASE(
    "Project clock gate releases only after presentation",
    "[editor-runtime][clock]"
) {
    ProjectPresentationSignal signal;
    ProjectClockGate gate(signal);
    Time time;
    FixedTime fixed_time;
    fixed_time.accumulate_overstep(0.5F);

    gate.arm(time);
    CHECK(gate.armed());
    CHECK_FALSE(gate.released());
    CHECK(time.time_scale == 0.0F);
    CHECK_FALSE(gate.release_if_presented(time, fixed_time));

    signal.present();
    CHECK(gate.release_if_presented(time, fixed_time));
    CHECK(gate.released());
    CHECK(time.time_scale == 1.0F);
    CHECK(fixed_time.elapsed_time() == 0.0F);
    CHECK(fixed_time.overstep() == 0.0F);
}

TEST_CASE(
    "Project clock gate preserves an existing pause",
    "[editor-runtime][clock][playtest]"
) {
    ProjectPresentationSignal signal;
    ProjectClockGate gate(signal);
    Time time;
    FixedTime fixed_time;
    time.time_scale = 0.0F;

    gate.arm(time);
    signal.present();

    REQUIRE(gate.release_if_presented(time, fixed_time));
    CHECK(time.time_scale == 0.0F);
}

TEST_CASE(
    "Project fixed updates begin after a presented frame",
    "[editor-runtime][clock][fixed-update]"
) {
    ProjectPresentationSignal signal;
    auto fixed_updates = 0;

    App app;
    app.add_plugin<TimePlugin>();
    app.add_resource(ProjectClockGate(signal));
    app.add_systems(PreUpdate, release_project_clock);
    app.add_systems(FixedUpdate, [&fixed_updates]() {
        ++fixed_updates;
    });
    app.finish();

    auto& time = app.resource<Time>();
    time.set_fixed_delta(0.25F);
    app.resource<FixedTime>().set_timestep(0.25F);
    app.resource<ProjectClockGate>().arm(time);

    app.update();
    CHECK(fixed_updates == 0);

    signal.present();
    app.update();
    CHECK(fixed_updates == 0);

    app.update();
    CHECK(fixed_updates == 1);
}
