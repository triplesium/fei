#include "core/time.hpp"

#include "app/app.hpp"

#include <cmath>
#include <stdexcept>

namespace fei {

void Time::tick() {
    auto now = std::chrono::steady_clock::now();
    if (m_fixed_delta) {
        m_delta_time = *m_fixed_delta * time_scale;
        m_elapsed_time += *m_fixed_delta;
        m_last_tick_time = now;
        return;
    }
    auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(
                        now - m_last_tick_time
    )
                        .count();
    m_delta_time = duration * time_scale;
    m_last_tick_time = now;
    m_elapsed_time = std::chrono::duration_cast<std::chrono::duration<float>>(
                         now - m_start_time
    )
                         .count();
}

float Time::delta() const {
    return m_delta_time;
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

void Time::reset_elapsed_time(float elapsed_time) {
    if (!std::isfinite(elapsed_time) || elapsed_time < 0.0f) {
        throw std::invalid_argument(
            "Elapsed time must be finite and non-negative"
        );
    }
    m_elapsed_time = elapsed_time;
    m_start_time =
        std::chrono::steady_clock::now() -
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(elapsed_time)
        );
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

void TimePlugin::setup(App& app) {
    app.add_resource<Time>();
    app.add_systems(First, time_system);
}

} // namespace fei
