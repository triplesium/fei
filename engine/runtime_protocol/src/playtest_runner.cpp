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
    if (busy()) {
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

Status<PlaytestError> PlaytestRunner::queue_segment(
    const PlaytestRegistry& registry,
    PlaytestSegmentRequest request,
    PlaytestSegmentProgram program
) {
    if (!registry.frozen()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Conflict,
                .message =
                    "Playtest segments require a frozen interface registry",
            }
        );
    }
    if (!m_enabled) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message =
                    "Playtest segments require a deterministic runtime session",
            }
        );
    }
    if (busy()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Conflict,
                .message = "A playtest operation is active or has an unread "
                           "completion",
            }
        );
    }
    if (request.request_id.empty()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest segment request ID must not be empty",
            }
        );
    }
    if (registry.find(request.interface_id) == nullptr) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message =
                    "Unknown playtest interface '" + request.interface_id + "'",
            }
        );
    }
    if (request.max_ticks == 0 || request.max_ticks > maximum_segment_ticks) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest segment max_ticks must be between 1 and " +
                           std::to_string(maximum_segment_ticks),
            }
        );
    }
    if (!program.next) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Internal,
                .message = "Playtest segment program is not executable",
            }
        );
    }
    m_segment = Segment {
        .request = std::move(request),
        .program = std::move(program),
    };
    return {};
}

void PlaytestRunner::begin_queued_step(
    World& world,
    const PlaytestRegistry& registry
) {
    if (m_segment && !m_segment->started) {
        const auto* interface = registry.find(m_segment->request.interface_id);
        if (interface == nullptr) {
            fail_segment(
                world,
                registry,
                "observe",
                PlaytestError {
                    .kind = PlaytestErrorKind::Unsupported,
                    .message = "Playtest interface disappeared before segment "
                               "execution",
                }
            );
            return;
        }
        try {
            auto observation = interface->observe(world);
            if (!observation) {
                fail_segment(
                    world,
                    registry,
                    "observe",
                    std::move(observation.error())
                );
                return;
            }
            m_segment->observation_json = std::move(*observation);
            m_segment->started = true;
            continue_segment(world, registry);
        } catch (const std::exception& error) {
            fail_segment(
                world,
                registry,
                "observe",
                internal_error("observe", error.what())
            );
        } catch (...) {
            fail_segment(
                world,
                registry,
                "observe",
                unknown_internal_error("observe")
            );
        }
        return;
    }
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
    if (m_segment && m_segment->started && m_segment->action_active) {
        ++m_segment->completed_ticks;
        auto ended = end_segment_action(world, registry);
        if (!ended) {
            fail_segment(world, registry, "end_step", std::move(ended.error()));
            return;
        }
        const auto* interface = registry.find(m_segment->request.interface_id);
        if (interface == nullptr) {
            fail_segment(
                world,
                registry,
                "observe",
                PlaytestError {
                    .kind = PlaytestErrorKind::Unsupported,
                    .message = "Playtest interface disappeared during segment "
                               "execution",
                }
            );
            return;
        }
        try {
            auto observation = interface->observe(world);
            if (!observation) {
                fail_segment(
                    world,
                    registry,
                    "observe",
                    std::move(observation.error())
                );
                return;
            }
            m_segment->observation_json = std::move(*observation);
            continue_segment(world, registry);
        } catch (const std::exception& error) {
            fail_segment(
                world,
                registry,
                "observe",
                internal_error("observe", error.what())
            );
        } catch (...) {
            fail_segment(
                world,
                registry,
                "observe",
                unknown_internal_error("observe")
            );
        }
        return;
    }
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

Optional<PlaytestSegmentProgress> PlaytestRunner::segment_progress() const {
    if (!m_segment) {
        return {};
    }
    return PlaytestSegmentProgress {
        .request_id = m_segment->request.request_id,
        .interface_id = m_segment->request.interface_id,
        .state = m_segment->started ? PlaytestSegmentState::Running :
                                      PlaytestSegmentState::Pending,
        .completed_ticks = m_segment->completed_ticks,
        .max_ticks = m_segment->request.max_ticks,
    };
}

Optional<PlaytestSegmentCompletion> PlaytestRunner::take_segment_completion() {
    if (!m_segment_completion) {
        return {};
    }
    auto completion = std::move(*m_segment_completion);
    m_segment_completion.reset();
    return completion;
}

Status<PlaytestError> PlaytestRunner::end_segment_action(
    World& world,
    const PlaytestRegistry& registry
) {
    if (!m_segment || !m_segment->action_active) {
        return {};
    }
    const auto* interface = registry.find(m_segment->request.interface_id);
    if (interface == nullptr) {
        m_segment->action_active = false;
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message = "Playtest interface disappeared while cleaning up "
                           "an action",
            }
        );
    }
    try {
        auto ended = interface->end_step(world);
        m_segment->action_active = false;
        return ended;
    } catch (const std::exception& error) {
        m_segment->action_active = false;
        return failure(internal_error("end_step", error.what()));
    } catch (...) {
        m_segment->action_active = false;
        return failure(unknown_internal_error("end_step"));
    }
}

void PlaytestRunner::continue_segment(
    World& world,
    const PlaytestRegistry& registry
) {
    if (!m_segment) {
        return;
    }
    Result<PlaytestSegmentDecision, PlaytestError> decision = failure(
        PlaytestError {
            .kind = PlaytestErrorKind::Internal,
            .message = "Segment program did not return a decision",
        }
    );
    try {
        decision = m_segment->program.next(
            m_segment->observation_json,
            m_segment->completed_ticks
        );
    } catch (const std::exception& error) {
        fail_segment(
            world,
            registry,
            "program",
            internal_error("program", error.what())
        );
        return;
    } catch (...) {
        fail_segment(
            world,
            registry,
            "program",
            unknown_internal_error("program")
        );
        return;
    }
    if (!decision) {
        fail_segment(world, registry, "program", std::move(decision.error()));
        return;
    }
    if (decision->kind == PlaytestSegmentDecisionKind::Stop) {
        pause_playtest_clock(world);
        finish_segment(
            PlaytestSegmentState::Stopped,
            std::move(decision->value),
            std::move(m_segment->observation_json)
        );
        return;
    }
    if (m_segment->completed_ticks >= m_segment->request.max_ticks) {
        pause_playtest_clock(world);
        finish_segment(
            PlaytestSegmentState::MaxTicks,
            "max_ticks",
            std::move(m_segment->observation_json)
        );
        return;
    }

    const auto* interface = registry.find(m_segment->request.interface_id);
    if (interface == nullptr) {
        fail_segment(
            world,
            registry,
            "begin_step",
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message =
                    "Playtest interface disappeared during segment execution",
            }
        );
        return;
    }
    try {
        auto begun = interface->begin_step(world, decision->value);
        if (!begun) {
            // The callback may have partially changed control state. Give its
            // paired cleanup callback one best-effort chance to release it.
            m_segment->action_active = true;
            (void)end_segment_action(world, registry);
            fail_segment(
                world,
                registry,
                "begin_step",
                std::move(begun.error())
            );
            return;
        }
        m_segment->action_active = true;
        resume_playtest_clock(world);
    } catch (const std::exception& error) {
        m_segment->action_active = true;
        (void)end_segment_action(world, registry);
        fail_segment(
            world,
            registry,
            "begin_step",
            internal_error("begin_step", error.what())
        );
    } catch (...) {
        m_segment->action_active = true;
        (void)end_segment_action(world, registry);
        fail_segment(
            world,
            registry,
            "begin_step",
            unknown_internal_error("begin_step")
        );
    }
}

void PlaytestRunner::finish_segment(
    PlaytestSegmentState state,
    std::string reason,
    std::string observation_json
) {
    const auto request_id = m_segment->request.request_id;
    const auto interface_id = m_segment->request.interface_id;
    const auto completed_ticks = m_segment->completed_ticks;
    const auto max_ticks = m_segment->request.max_ticks;
    m_segment.reset();
    m_segment_completion = PlaytestSegmentCompletion {
        .request_id = request_id,
        .interface_id = interface_id,
        .state = state,
        .completed_ticks = completed_ticks,
        .max_ticks = max_ticks,
        .reason = std::move(reason),
        .observation_json = std::move(observation_json),
    };
}

void PlaytestRunner::fail_segment(
    World& world,
    const PlaytestRegistry& registry,
    std::string phase,
    PlaytestError error
) {
    if (!m_segment) {
        return;
    }
    if (m_segment->action_active) {
        auto cleaned = end_segment_action(world, registry);
        if (!cleaned) {
            error.message += "; cleanup failed: " + cleaned.error().message;
        }
    }
    pause_playtest_clock(world);
    const auto request_id = m_segment->request.request_id;
    const auto interface_id = m_segment->request.interface_id;
    const auto completed_ticks = m_segment->completed_ticks;
    const auto max_ticks = m_segment->request.max_ticks;
    const auto observation_json = std::move(m_segment->observation_json);
    m_segment.reset();
    m_segment_completion = PlaytestSegmentCompletion {
        .request_id = request_id,
        .interface_id = interface_id,
        .state = PlaytestSegmentState::Failed,
        .completed_ticks = completed_ticks,
        .max_ticks = max_ticks,
        .observation_json = observation_json,
        .error_phase = std::move(phase),
        .error = std::move(error),
    };
}

Status<PlaytestError> PlaytestRunner::cancel_segment(
    World& world,
    const PlaytestRegistry& registry,
    std::string_view request_id
) {
    if (!m_segment || m_segment->request.request_id != request_id) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Unsupported,
                .message = "Unknown active playtest segment '" +
                           std::string(request_id) + "'",
            }
        );
    }
    auto ended = end_segment_action(world, registry);
    if (!ended) {
        auto error = std::move(ended.error());
        fail_segment(world, registry, "cancel", std::move(error));
        return {};
    }
    const auto* interface = registry.find(m_segment->request.interface_id);
    if (interface != nullptr) {
        try {
            auto observation = interface->observe(world);
            if (observation) {
                m_segment->observation_json = std::move(*observation);
            }
        } catch (...) {
            // Cancellation still releases control and pauses even if a final
            // observation cannot be refreshed.
            m_segment->observation_json.clear();
        }
    }
    pause_playtest_clock(world);
    finish_segment(
        PlaytestSegmentState::Cancelled,
        "cancelled",
        std::move(m_segment->observation_json)
    );
    return {};
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
