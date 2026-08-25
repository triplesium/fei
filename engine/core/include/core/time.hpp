#pragma once
#include "app/plugin.hpp"
#include "base/optional.hpp"
#include "ecs/system_params.hpp"
#include "refl/reflect.hpp"

#include <chrono>

namespace ets {

struct TimeSnapshotState {
    float delta {};
    float elapsed {};
    float max_delta {0.25F};
    Optional<float> fixed_delta;
    float time_scale {1.0F};
};

struct FixedTimeSnapshotState {
    double timestep {1.0 / 60.0};
    double overstep {};
    double elapsed {};
};

ETS_REFLECT(Resource, ScriptPrelude)
struct Time {
    Time() = default;

    void tick();
    float delta() const;
    float elapsed_time() const { return m_elapsed_time; }

    void set_fixed_delta(float delta);
    void clear_fixed_delta();
    void set_max_delta(float delta);
    void reset_elapsed_time(float elapsed_time = 0.0f);
    [[nodiscard]] Optional<float> fixed_delta() const { return m_fixed_delta; }
    float max_delta() const { return m_max_delta; }
    [[nodiscard]] TimeSnapshotState snapshot_state() const;
    void restore_snapshot_state(const TimeSnapshotState& state);

    float time_scale {1.0f};

  private:
    Optional<std::chrono::steady_clock::time_point> m_last_tick_time;
    float m_delta_time = 0.0f;
    float m_elapsed_time = 0.0f;
    float m_max_delta = 0.25f;
    Optional<float> m_fixed_delta;
};

ETS_REFLECT(Resource, ScriptPrelude)
class FixedTime {
  public:
    FixedTime() = default;
    explicit FixedTime(float timestep_seconds);

    float delta() const { return static_cast<float>(m_timestep); }
    float timestep() const { return static_cast<float>(m_timestep); }
    float elapsed_time() const { return static_cast<float>(m_elapsed_time); }
    float overstep() const { return static_cast<float>(m_overstep); }
    float overstep_fraction() const;

    void set_timestep(float timestep_seconds);
    void set_timestep_hz(float hz);
    void accumulate_overstep(float delta_seconds);
    bool expend();
    void reset();
    [[nodiscard]] FixedTimeSnapshotState snapshot_state() const;
    void restore_snapshot_state(const FixedTimeSnapshotState& state);

  private:
    double m_timestep {1.0 / 60.0};
    double m_overstep {0.0};
    double m_elapsed_time {0.0};
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

void time_system(ResRW<Time> time);

ETS_REFLECT(Plugin)
class TimePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace ets
