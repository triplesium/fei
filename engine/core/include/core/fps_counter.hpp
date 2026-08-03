#pragma once

#include "app/app.hpp"
#include "core/time.hpp"
#include "ecs/system_params.hpp"

#include <cstdint>

namespace fei {

struct FpsCounter {
    float fps {0.0f};
    float frame_time_seconds {0.0f};

    void tick(float delta_time_seconds) {
        frame_time_seconds = delta_time_seconds;
        if (delta_time_seconds <= 0.0f) {
            return;
        }

        m_accumulated_time += delta_time_seconds;
        ++m_frame_count;

        if (m_accumulated_time >= m_update_interval_seconds) {
            fps = static_cast<float>(m_frame_count) / m_accumulated_time;
            m_accumulated_time = 0.0f;
            m_frame_count = 0;
        }
    }

  private:
    float m_accumulated_time {0.0f};
    std::uint32_t m_frame_count {0};
    float m_update_interval_seconds {0.5f};
};

inline void
fps_counter_system(ResRO<Time> time, ResRW<FpsCounter> fps_counter) {
    fps_counter->tick(time->delta());
}

class FpsCounterPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_resource<FpsCounter>();
        app.add_systems(PostUpdate, fps_counter_system);
    }
};

} // namespace fei
