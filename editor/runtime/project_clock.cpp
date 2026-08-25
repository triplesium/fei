#include "project_clock.hpp"

#include "core/time.hpp"

namespace ets::editor_runtime {

void ProjectPresentationSignal::present() const {
    m_presented->store(true, std::memory_order_release);
}

bool ProjectPresentationSignal::presented() const {
    return m_presented->load(std::memory_order_acquire);
}

ProjectClockGate::ProjectClockGate(ProjectPresentationSignal signal) :
    m_signal(signal) {}

void ProjectClockGate::arm(Time& time) {
    if (m_armed) {
        return;
    }
    m_previous_time_scale = time.time_scale;
    time.time_scale = 0.0F;
    m_armed = true;
}

bool ProjectClockGate::release_if_presented(Time& time, FixedTime& fixed_time) {
    if (!m_armed || m_released || !m_signal.presented()) {
        return false;
    }
    fixed_time.reset();
    time.time_scale = m_previous_time_scale;
    m_released = true;
    return true;
}

} // namespace ets::editor_runtime
