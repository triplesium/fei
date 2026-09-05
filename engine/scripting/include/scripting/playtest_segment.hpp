#pragma once

#include "base/result.hpp"
#include "base/types.hpp"
#include "scripting/error.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace ets {

enum class LuauPlaytestSegmentDecisionKind : uint8 {
    Action,
    Stop,
};

struct LuauPlaytestSegmentDecision {
    LuauPlaytestSegmentDecisionKind kind {
        LuauPlaytestSegmentDecisionKind::Action
    };
    std::string value;
};

class LuauPlaytestSegment {
  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    explicit LuauPlaytestSegment(std::unique_ptr<Impl> impl);

  public:
    static constexpr std::size_t maximum_source_bytes = std::size_t {64} * 1024;
    static constexpr std::size_t maximum_observation_bytes =
        std::size_t {1} * 1024 * 1024;
    static constexpr std::size_t maximum_vm_bytes =
        std::size_t {16} * 1024 * 1024;
    static constexpr uint32 invocation_interrupt_budget = 10'000;

    static Result<std::shared_ptr<LuauPlaytestSegment>, LuauScriptError>
    compile(std::string_view source);

    ~LuauPlaytestSegment();
    LuauPlaytestSegment(const LuauPlaytestSegment&) = delete;
    LuauPlaytestSegment& operator=(const LuauPlaytestSegment&) = delete;

    Result<LuauPlaytestSegmentDecision, LuauScriptError>
    next(std::string_view observation_json, uint32 tick);
};

} // namespace ets
