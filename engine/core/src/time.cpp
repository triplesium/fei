#include "core/time.hpp"

#include "app/app.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ets {

void Time::tick() {
    auto now = std::chrono::steady_clock::now();
    float raw_delta = 0.0f;
    if (m_fixed_delta) {
        raw_delta = *m_fixed_delta;
    } else {
        raw_delta = std::chrono::duration_cast<std::chrono::duration<float>>(
                        now - m_last_tick_time
        )
                        .count();
    }

    m_delta_time = std::min(raw_delta * time_scale, m_max_delta);
    m_elapsed_time += m_delta_time;
    m_last_tick_time = now;
}

float Time::delta() const {
    return m_delta_time;
}

TimeSnapshotState Time::snapshot_state() const {
    return TimeSnapshotState {
        .delta = m_delta_time,
        .elapsed = m_elapsed_time,
        .max_delta = m_max_delta,
        .fixed_delta = m_fixed_delta,
        .time_scale = time_scale,
    };
}

void Time::restore_snapshot_state(const TimeSnapshotState& state) {
    m_delta_time = state.delta;
    m_elapsed_time = state.elapsed;
    m_max_delta = state.max_delta;
    m_fixed_delta = state.fixed_delta;
    time_scale = state.time_scale;
    m_last_tick_time = std::chrono::steady_clock::now();
}

void Time::set_fixed_delta(float delta) {
    if (!std::isfinite(delta) || delta <= 0.0f) {
        throw std::invalid_argument(
            "Fixed time delta must be finite and positive"
        );
    }
    m_fixed_delta = delta;
    m_last_tick_time = std::chrono::steady_clock::now();
}

void Time::clear_fixed_delta() {
    m_fixed_delta.reset();
    m_last_tick_time = std::chrono::steady_clock::now();
}

void Time::set_max_delta(float delta) {
    if (!std::isfinite(delta) || delta <= 0.0f) {
        throw std::invalid_argument(
            "Maximum time delta must be finite and positive"
        );
    }
    m_max_delta = delta;
}

void Time::reset_elapsed_time(float elapsed_time) {
    if (!std::isfinite(elapsed_time) || elapsed_time < 0.0f) {
        throw std::invalid_argument(
            "Elapsed time must be finite and non-negative"
        );
    }
    m_elapsed_time = elapsed_time;
}

FixedTime::FixedTime(float timestep_seconds) {
    set_timestep(timestep_seconds);
}

float FixedTime::overstep_fraction() const {
    return static_cast<float>(m_overstep / m_timestep);
}

void FixedTime::set_timestep(float timestep_seconds) {
    if (!std::isfinite(timestep_seconds) || timestep_seconds <= 0.0f) {
        throw std::invalid_argument(
            "Fixed timestep must be finite and positive"
        );
    }
    m_timestep = timestep_seconds;
}

void FixedTime::set_timestep_hz(float hz) {
    if (!std::isfinite(hz) || hz <= 0.0f) {
        throw std::invalid_argument(
            "Fixed timestep frequency must be finite and positive"
        );
    }
    set_timestep(1.0f / hz);
}

void FixedTime::accumulate_overstep(float delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) {
        throw std::invalid_argument(
            "Fixed time overstep delta must be finite and non-negative"
        );
    }
    m_overstep += delta_seconds;
}

bool FixedTime::expend() {
    if (m_overstep < m_timestep) {
        return false;
    }
    m_overstep -= m_timestep;
    m_elapsed_time += m_timestep;
    return true;
}

void FixedTime::reset() {
    m_overstep = 0.0;
    m_elapsed_time = 0.0;
}

FixedTimeSnapshotState FixedTime::snapshot_state() const {
    return FixedTimeSnapshotState {
        .timestep = m_timestep,
        .overstep = m_overstep,
        .elapsed = m_elapsed_time,
    };
}

void FixedTime::restore_snapshot_state(const FixedTimeSnapshotState& state) {
    m_timestep = state.timestep;
    m_overstep = state.overstep;
    m_elapsed_time = state.elapsed;
}

Timer::Timer(float duration_seconds, TimerMode mode) :
    duration(duration_seconds), mode(mode) {}

void Timer::tick(float delta) {
    if (m_just_finished) {
        m_just_finished = false;
    }
    if (mode == Once && m_time >= duration) {
        return;
    }
    m_time += delta;
    if (m_time >= duration) {
        m_just_finished = true;
        if (mode == Repeating) {
            if (duration > 0.0f) {
                while (m_time >= duration) {
                    m_time -= duration;
                }
            } else {
                m_time = 0.0f;
            }
        } else {
            m_time = duration;
        }
    }
}

bool Timer::just_finished() const {
    return m_just_finished;
}

void time_system(ResRW<Time> time) {
    time->tick();
}

namespace {

void run_fixed_main_schedule(WorldRef world) {
    const auto delta =
        static_cast<const World&>(*world).resource<Time>().delta();
    world->resource_untracked(type_id<FixedTime>())
        .get<FixedTime>()
        .accumulate_overstep(delta);

    while (world->resource_untracked(type_id<FixedTime>())
               .get<FixedTime>()
               .expend()) {
        world->run_schedule(FixedFirst);
        world->run_schedule(FixedPreUpdate);
        world->run_schedule(FixedUpdate);
        world->run_schedule(FixedPostUpdate);
        world->run_schedule(FixedLast);
    }
}

} // namespace

void TimePlugin::setup(App& app) {
    app.add_resource<Time>();
    app.add_resource<FixedTime>();
    app.add_systems(First, time_system);
    app.add_systems(
        RunFixedMainLoop,
        run_fixed_main_schedule |
            in_set<RunFixedMainLoopSystems::FixedMainLoop>()
    );
}

} // namespace ets
