#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "runtime_protocol/playtest.hpp"

#include <string>
#include <string_view>

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

struct PlaytestSegmentRequest {
    std::string request_id;
    std::string interface_id;
    uint32 max_ticks {0};
};

enum class PlaytestSegmentState : uint8 {
    Pending,
    Running,
    Stopped,
    MaxTicks,
    Cancelled,
    Failed,
};

struct PlaytestSegmentProgress {
    std::string request_id;
    std::string interface_id;
    PlaytestSegmentState state {PlaytestSegmentState::Pending};
    uint32 completed_ticks {0};
    uint32 max_ticks {0};
};

struct PlaytestSegmentCompletion {
    std::string request_id;
    std::string interface_id;
    PlaytestSegmentState state {PlaytestSegmentState::Failed};
    uint32 completed_ticks {0};
    uint32 max_ticks {0};
    std::string reason;
    std::string observation_json;
    std::string error_phase;
    Optional<PlaytestError> error;
};

class PlaytestRunner {
  public:
    void set_enabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool enabled() const { return m_enabled; }

    // A completion must be consumed before another step can be queued.
    [[nodiscard]] Status<PlaytestError>
    queue_step(const PlaytestRegistry& registry, PlaytestStepRequest request);

    static constexpr uint32 maximum_segment_ticks = 3'600;

    [[nodiscard]] Status<PlaytestError> queue_segment(
        const PlaytestRegistry& registry,
        PlaytestSegmentRequest request,
        PlaytestSegmentProgram program
    );
    [[nodiscard]] Status<PlaytestError> cancel_segment(
        World& world,
        const PlaytestRegistry& registry,
        std::string_view request_id
    );

    void begin_queued_step(World& world, const PlaytestRegistry& registry);
    void advance_fixed_tick(World& world, const PlaytestRegistry& registry);

    [[nodiscard]] Optional<PlaytestStepProgress> progress() const;
    [[nodiscard]] const PlaytestStepCompletion* completion() const {
        return m_completion ? &*m_completion : nullptr;
    }
    [[nodiscard]] Optional<PlaytestStepCompletion> take_completion();
    [[nodiscard]] Optional<PlaytestSegmentProgress> segment_progress() const;
    [[nodiscard]] const PlaytestSegmentCompletion* segment_completion() const {
        return m_segment_completion ? &*m_segment_completion : nullptr;
    }
    [[nodiscard]] Optional<PlaytestSegmentCompletion> take_segment_completion();
    [[nodiscard]] bool busy() const {
        return m_step.has_value() || m_completion.has_value() ||
               m_segment.has_value() || m_segment_completion.has_value();
    }

  private:
    struct Step {
        PlaytestStepRequest request;
        uint32 target_ticks {0};
        uint32 completed_ticks {0};
        bool started {false};
    };

    struct Segment {
        PlaytestSegmentRequest request;
        PlaytestSegmentProgram program;
        uint32 completed_ticks {0};
        bool started {false};
        bool action_active {false};
        std::string observation_json;
    };

    void complete(PlaytestStepResult result);
    void fail(PlaytestError error);
    void continue_segment(World& world, const PlaytestRegistry& registry);
    void finish_segment(
        PlaytestSegmentState state,
        std::string reason,
        std::string observation_json
    );
    void fail_segment(
        World& world,
        const PlaytestRegistry& registry,
        std::string phase,
        PlaytestError error
    );
    [[nodiscard]] Status<PlaytestError>
    end_segment_action(World& world, const PlaytestRegistry& registry);

    Optional<Step> m_step;
    Optional<PlaytestStepCompletion> m_completion;
    Optional<Segment> m_segment;
    Optional<PlaytestSegmentCompletion> m_segment_completion;
    bool m_enabled {true};
};

} // namespace runtime_protocol
} // namespace ets
