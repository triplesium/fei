#include "runtime_protocol/playtest_plugin.hpp"

#include "app/app.hpp"
#include "base/optional.hpp"
#include "core/time.hpp"
#include "ecs/system_params.hpp"

#include <utility>

namespace ets::runtime_protocol {
namespace {

struct PlaytestClock {
    float previous_time_scale {1.0F};
    Optional<float> previous_fixed_delta;
    bool enabled {false};
};

void begin_playtest_step(WorldRef world) {
    auto& runner = world->resource<PlaytestRunner>();
    const auto& registry =
        static_cast<const World&>(*world).resource<PlaytestRegistry>();
    runner.begin_queued_step(*world, registry);
}

void complete_playtest_tick(WorldRef world) {
    auto& runner = world->resource<PlaytestRunner>();
    const auto& registry =
        static_cast<const World&>(*world).resource<PlaytestRegistry>();
    runner.advance_fixed_tick(*world, registry);
}

} // namespace

void PlaytestPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<TimePlugin>();
}

void PlaytestPlugin::setup(App& app) {
    if (!app.has_resource<PlaytestRegistry>()) {
        app.add_resource(PlaytestRegistry {});
    }
    if (!app.has_resource<PlaytestRunner>()) {
        app.add_resource(PlaytestRunner {});
    }
    auto& time = app.resource<Time>();
    app.add_resource(
        PlaytestClock {
            .previous_time_scale = time.time_scale,
            .previous_fixed_delta = time.fixed_delta(),
        }
    );
    app.add_systems(
           PreUpdate,
           begin_playtest_step | in_set<PlaytestSystems::BeginStep>()
    )
        .add_systems(
            FixedLast,
            complete_playtest_tick | in_set<PlaytestSystems::CompleteStep>()
        );
}

void PlaytestPlugin::finish(App& app) {
    playtest_registry(app).freeze();
    if (playtest_registry(app).interfaces().empty()) {
        return;
    }
    app.resource<PlaytestClock>().enabled = true;
    auto& time = app.resource<Time>();
    time.set_fixed_delta(app.resource<FixedTime>().timestep());
    time.time_scale = 0.0F;
}

void PlaytestPlugin::cleanup(App& app) noexcept {
    if (!app.has_resource<PlaytestClock>() || !app.has_resource<Time>()) {
        return;
    }
    const auto& clock = app.resource<PlaytestClock>();
    if (!clock.enabled) {
        return;
    }
    auto& time = app.resource<Time>();
    time.time_scale = clock.previous_time_scale;
    if (clock.previous_fixed_delta) {
        time.set_fixed_delta(*clock.previous_fixed_delta);
    } else {
        time.clear_fixed_delta();
    }
}

void pause_playtest_clock(World& world) {
    if (world.has_resource<PlaytestClock>() && world.has_resource<Time>() &&
        world.resource<PlaytestClock>().enabled) {
        world.resource<Time>().time_scale = 0.0F;
    }
}

void resume_playtest_clock(World& world) {
    if (world.has_resource<PlaytestClock>() && world.has_resource<Time>() &&
        world.resource<PlaytestClock>().enabled) {
        world.resource<Time>().time_scale = 1.0F;
    }
}

Status<PlaytestError> register_playtest_interface(
    App& app,
    PlaytestInterfaceRegistration registration
) {
    if (!app.has_resource<PlaytestRegistry>()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message = "PlaytestPlugin must be installed before "
                           "registering a playtest interface",
            }
        );
    }
    return playtest_registry(app).add(std::move(registration));
}

PlaytestRegistry& playtest_registry(App& app) {
    return app.resource<PlaytestRegistry>();
}

const PlaytestRegistry& playtest_registry(const App& app) {
    return app.world().resource<PlaytestRegistry>();
}

PlaytestRunner& playtest_runner(App& app) {
    return app.resource<PlaytestRunner>();
}

const PlaytestRunner& playtest_runner(const App& app) {
    return app.world().resource<PlaytestRunner>();
}

} // namespace ets::runtime_protocol
