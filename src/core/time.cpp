#include "core/time.hpp"

namespace fei {

void Time::tick() {
    auto now = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::duration<float>>(
            now - m_last_tick_time
        )
            .count();
    m_delta_time = duration * time_scale;
    m_last_tick_time = now;
}

float Time::delta() const {
    return m_delta_time;
}

Timer::Timer(float duration_seconds, TimerMode mode) :
    duration(duration_seconds), mode(mode) {}

void Timer::tick(float delta) {
    if (m_just_finished) {
        m_just_finished = false;
    }
    m_time += delta;
    if (m_time >= duration) {
        m_time -= duration;
        m_just_finished = true;
    }
}

bool Timer::just_finished() const {
    return m_just_finished;
}

void time_system(Res<Time> time) {
    time->tick();
}

void TimePlugin::setup(App& app) {
    app.add_resource<Time>();
    app.add_system(First, time_system);
}

}
