#pragma once

#include "base/result.hpp"
#include "base/types.hpp"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ets {

class World;

namespace runtime_protocol {

enum class PlaytestErrorKind : uint8 {
    InvalidAction,
    Conflict,
    Unsupported,
    Internal,
};

struct PlaytestError {
    PlaytestErrorKind kind {PlaytestErrorKind::Internal};
    std::string message;
};

[[nodiscard]] std::string_view playtest_error_kind_name(PlaytestErrorKind kind);

struct PlaytestInterfaceDescriptor {
    std::string id;
    std::string label;
    std::string description;
    uint32 decision_ticks {1};
    uint32 minimum_ticks {1};
    uint32 maximum_ticks {1};
    bool allow_tick_override {false};
    std::string action_schema_json;
    std::string observation_schema_json;
};

using PlaytestBeginStep =
    std::function<Status<PlaytestError>(World&, std::string_view action_json)>;
using PlaytestEndStep = std::function<Status<PlaytestError>(World&)>;
using PlaytestObserve =
    std::function<Result<std::string, PlaytestError>(World&)>;

struct PlaytestInterfaceRegistration {
    PlaytestInterfaceDescriptor descriptor;
    PlaytestBeginStep begin_step;
    PlaytestEndStep end_step;
    PlaytestObserve observe;
};

enum class PlaytestSegmentDecisionKind : uint8 {
    Action,
    Stop,
};

struct PlaytestSegmentDecision {
    PlaytestSegmentDecisionKind kind {PlaytestSegmentDecisionKind::Action};
    // JSON for Action, a human-readable reason for Stop.
    std::string value;
};

using PlaytestSegmentNext = std::function<
    Result<PlaytestSegmentDecision, PlaytestError>(std::string_view, uint32)>;

struct PlaytestSegmentProgram {
    PlaytestSegmentNext next;
};

struct PlaytestSegmentCompiler {
    using Compile = std::function<
        Result<PlaytestSegmentProgram, PlaytestError>(std::string_view)>;

    Compile compile;
};

class PlaytestRegistry {
  public:
    Status<PlaytestError> add(PlaytestInterfaceRegistration registration);

    [[nodiscard]] const PlaytestInterfaceRegistration*
    find(std::string_view id) const;

    [[nodiscard]] std::span<const PlaytestInterfaceRegistration>
    interfaces() const {
        return m_interfaces;
    }

    void freeze() noexcept { m_frozen = true; }
    [[nodiscard]] bool frozen() const noexcept { return m_frozen; }

  private:
    std::vector<PlaytestInterfaceRegistration> m_interfaces;
    std::unordered_map<std::string, std::size_t> m_indices;
    bool m_frozen {false};
};

} // namespace runtime_protocol
} // namespace ets
