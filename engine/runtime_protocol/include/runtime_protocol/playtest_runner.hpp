#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "runtime_protocol/playtest.hpp"

#include <string>

namespace ets {

class World;

namespace runtime_protocol {

struct PlaytestStepRequest {
    std::string request_id;
    std::string interface_id;
    std::string action_json;
    Optional<uint32> ticks;
};

struct PlaytestStepResult {
    std::string request_id;
    std::string interface_id;
    uint32 ticks {0};
    std::string observation_json;
};

struct PlaytestStepCompletion {
    std::string request_id;
    std::string interface_id;
    uint32 completed_ticks {0};
    uint32 target_ticks {0};
    Result<PlaytestStepResult, PlaytestError> result;
};

struct PlaytestStepProgress {
    std::string request_id;
    std::string interface_id;
    uint32 completed_ticks {0};
    uint32 target_ticks {0};
    bool started {false};
};

class PlaytestRunner {
  public:
    void set_enabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool enabled() const { return m_enabled; }

    // A completion must be consumed before another step can be queued.
    [[nodiscard]] Status<PlaytestError>
    queue_step(const PlaytestRegistry& registry, PlaytestStepRequest request);

    void begin_queued_step(World& world, const PlaytestRegistry& registry);
    void advance_fixed_tick(World& world, const PlaytestRegistry& registry);

    [[nodiscard]] Optional<PlaytestStepProgress> progress() const;
    [[nodiscard]] const PlaytestStepCompletion* completion() const {
        return m_completion ? &*m_completion : nullptr;
    }
    [[nodiscard]] Optional<PlaytestStepCompletion> take_completion();
    [[nodiscard]] bool busy() const {
        return m_step.has_value() || m_completion.has_value();
    }

  private:
    struct Step {
        PlaytestStepRequest request;
        uint32 target_ticks {0};
        uint32 completed_ticks {0};
        bool started {false};
    };

    void complete(PlaytestStepResult result);
    void fail(PlaytestError error);

    Optional<Step> m_step;
    Optional<PlaytestStepCompletion> m_completion;
    bool m_enabled {true};
};

} // namespace runtime_protocol
} // namespace ets
