#pragma once
#include "app/plugin.hpp"
#include "ecs/system_params.hpp"

#include <chrono>

namespace fei {

struct Time {
    void tick();
    float delta() const;
    float elapsed_time() const { return m_elapsed_time; }

    float time_scale {1.0f};

  private:
    std::chrono::steady_clock::time_point m_last_tick_time {
        std::chrono::steady_clock::now()
    };
    std::chrono::steady_clock::time_point m_start_time {
        std::chrono::steady_clock::now()
    };
    float m_delta_time = 0.0f;
    float m_elapsed_time = 0.0f;
};

enum TimerMode {
    Once,
    Repeating,
};

class Timer {
  public:
    Timer(float duration_seconds, TimerMode mode);

    void tick(float delta);

    bool just_finished() const;

    float duration;
    TimerMode mode;

  private:
    float m_time {0.0f};
    bool m_just_finished {false};
};

void time_system(Res<Time> time);

class TimePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
