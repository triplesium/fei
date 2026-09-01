#include "runtime_protocol/playtest_runner.hpp"

#include "ecs/world.hpp"
#include "runtime_protocol/playtest_plugin.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace ets::runtime_protocol {
namespace {

PlaytestError internal_error(std::string_view phase, const char* message) {
    return PlaytestError {
        .kind = PlaytestErrorKind::Internal,
        .message = "Playtest " + std::string(phase) + " failed: " + message,
    };
}

PlaytestError unknown_internal_error(std::string_view phase) {
    return PlaytestError {
        .kind = PlaytestErrorKind::Internal,
        .message =
            "Playtest " + std::string(phase) + " failed with an unknown error",
    };
}

} // namespace

Status<PlaytestError> PlaytestRunner::queue_step(
    const PlaytestRegistry& registry,
    PlaytestStepRequest request
) {
    if (!registry.frozen()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Conflict,
                .message = "Playtest steps require a frozen interface registry",
            }
        );
    }
    if (!m_enabled) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message =
                    "Playtest steps require a deterministic runtime session",
            }
        );
    }
    if (m_step || m_completion) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Conflict,
                .message =
                    "A playtest step is active or has an unread completion",
            }
        );
    }
    if (request.request_id.empty()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest step request ID must not be empty",
            }
        );
    }
    const auto* interface = registry.find(request.interface_id);
    if (interface == nullptr) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message =
                    "Unknown playtest interface '" + request.interface_id + "'",
            }
        );
    }

    const auto& descriptor = interface->descriptor;
    const auto ticks = request.ticks.value_or(descriptor.decision_ticks);
    if (ticks < descriptor.minimum_ticks || ticks > descriptor.maximum_ticks) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest step ticks must be between " +
                           std::to_string(descriptor.minimum_ticks) + " and " +
                           std::to_string(descriptor.maximum_ticks),
            }
        );
    }
    if (!descriptor.allow_tick_override && ticks != descriptor.decision_ticks) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest interface '" + descriptor.id +
                           "' does not allow a tick override",
            }
        );
    }

    m_step = Step {
        .request = std::move(request),
        .target_ticks = ticks,
    };
    return {};
}

void PlaytestRunner::begin_queued_step(
    World& world,
    const PlaytestRegistry& registry
) {
    if (!m_step || m_step->started) {
        return;
    }
    const auto* interface = registry.find(m_step->request.interface_id);
    if (interface == nullptr) {
        fail(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message = "Playtest interface disappeared before execution",
            }
        );
        return;
    }
    try {
        auto begun = interface->begin_step(world, m_step->request.action_json);
        if (!begun) {
            pause_playtest_clock(world);
            fail(std::move(begun.error()));
            return;
        }
        m_step->started = true;
        resume_playtest_clock(world);
    } catch (const std::exception& error) {
        pause_playtest_clock(world);
        fail(internal_error("begin_step", error.what()));
    } catch (...) {
        pause_playtest_clock(world);
        fail(unknown_internal_error("begin_step"));
    }
}

void PlaytestRunner::advance_fixed_tick(
    World& world,
    const PlaytestRegistry& registry
) {
    if (!m_step || !m_step->started) {
        return;
    }
    ++m_step->completed_ticks;
    if (m_step->completed_ticks < m_step->target_ticks) {
        return;
    }

    const auto* interface = registry.find(m_step->request.interface_id);
    if (interface == nullptr) {
        pause_playtest_clock(world);
        fail(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message = "Playtest interface disappeared during execution",
            }
        );
        return;
    }
    try {
        auto ended = interface->end_step(world);
        if (!ended) {
            pause_playtest_clock(world);
            fail(std::move(ended.error()));
            return;
        }
        auto observation = interface->observe(world);
        if (!observation) {
            pause_playtest_clock(world);
            fail(std::move(observation.error()));
            return;
        }
        pause_playtest_clock(world);
        complete(
            PlaytestStepResult {
                .request_id = m_step->request.request_id,
                .interface_id = m_step->request.interface_id,
                .ticks = m_step->completed_ticks,
                .observation_json = std::move(*observation),
            }
        );
    } catch (const std::exception& error) {
        pause_playtest_clock(world);
        fail(internal_error("completion", error.what()));
    } catch (...) {
        pause_playtest_clock(world);
        fail(unknown_internal_error("completion"));
    }
}

Optional<PlaytestStepProgress> PlaytestRunner::progress() const {
    if (!m_step) {
        return {};
    }
    return PlaytestStepProgress {
        .request_id = m_step->request.request_id,
        .interface_id = m_step->request.interface_id,
        .completed_ticks = m_step->completed_ticks,
        .target_ticks = m_step->target_ticks,
        .started = m_step->started,
    };
}

Optional<PlaytestStepCompletion> PlaytestRunner::take_completion() {
    if (!m_completion) {
        return {};
    }
    auto completion = std::move(*m_completion);
    m_completion.reset();
    return completion;
}

void PlaytestRunner::complete(PlaytestStepResult result) {
    const auto request_id = result.request_id;
    const auto interface_id = result.interface_id;
    const auto ticks = result.ticks;
    m_step.reset();
    m_completion = PlaytestStepCompletion {
        .request_id = request_id,
        .interface_id = interface_id,
        .completed_ticks = ticks,
        .target_ticks = ticks,
        .result = std::move(result),
    };
}

void PlaytestRunner::fail(PlaytestError error) {
    auto request_id = m_step ? m_step->request.request_id : std::string {};
    auto interface_id = m_step ? m_step->request.interface_id : std::string {};
    const auto completed_ticks = m_step ? m_step->completed_ticks : 0;
    const auto target_ticks = m_step ? m_step->target_ticks : 0;
    m_step.reset();
    m_completion = PlaytestStepCompletion {
        .request_id = std::move(request_id),
        .interface_id = std::move(interface_id),
        .completed_ticks = completed_ticks,
        .target_ticks = target_ticks,
        .result = failure(std::move(error)),
    };
}

} // namespace ets::runtime_protocol
