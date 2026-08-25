#pragma once

#include <atomic>
#include <memory>

namespace ets {

class FixedTime;
struct Time;

} // namespace ets

namespace ets::editor_runtime {

class ProjectPresentationSignal {
  public:
    ProjectPresentationSignal() = default;

    void present() const;
    [[nodiscard]] bool presented() const;

  private:
    std::shared_ptr<std::atomic_bool> m_presented {
        std::make_shared<std::atomic_bool>(false)
    };
};

class ProjectClockGate {
  public:
    explicit ProjectClockGate(ProjectPresentationSignal signal);

    void arm(Time& time);
    bool release_if_presented(Time& time, FixedTime& fixed_time);

    [[nodiscard]] bool armed() const { return m_armed; }
    [[nodiscard]] bool released() const { return m_released; }

  private:
    ProjectPresentationSignal m_signal;
    float m_previous_time_scale {1.0F};
    bool m_armed {false};
    bool m_released {false};
};

} // namespace ets::editor_runtime
