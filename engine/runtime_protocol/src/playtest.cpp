#include "runtime_protocol/playtest.hpp"

#include "json_schema.hpp"

#include <exception>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace fei::runtime_protocol {
namespace {

using Json = nlohmann::json;

Status<PlaytestError>
validate_identifier(std::string_view name, std::string_view value) {
    if (value.empty()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = std::string(name) + " must not be empty",
            }
        );
    }
    constexpr std::size_t c_max_identifier_bytes = 128;
    if (value.size() > c_max_identifier_bytes) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = std::string(name) + " exceeds " +
                           std::to_string(c_max_identifier_bytes) + " bytes",
            }
        );
    }
    return {};
}

Result<Json, PlaytestError>
compile_schema(std::string_view name, const std::string& schema) {
    constexpr std::size_t c_max_schema_bytes = std::size_t {64} * 1024;
    if (schema.empty()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = std::string(name) + " must not be empty",
            }
        );
    }
    if (schema.size() > c_max_schema_bytes) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = std::string(name) + " exceeds " +
                           std::to_string(c_max_schema_bytes) + " bytes",
            }
        );
    }
    auto compiled = detail::compile_json_schema(schema);
    if (!compiled) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = std::string(name) + " " + compiled.error(),
            }
        );
    }
    return std::move(*compiled);
}

Status<PlaytestError>
validate_descriptor(const PlaytestInterfaceDescriptor& descriptor) {
    if (auto status =
            validate_identifier("Playtest interface ID", descriptor.id);
        !status) {
        return status;
    }
    if (auto status =
            validate_identifier("Playtest interface label", descriptor.label);
        !status) {
        return status;
    }
    if (descriptor.description.empty()) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest interface description must not be empty",
            }
        );
    }
    if (descriptor.minimum_ticks == 0 || descriptor.maximum_ticks == 0 ||
        descriptor.minimum_ticks > descriptor.maximum_ticks ||
        descriptor.decision_ticks < descriptor.minimum_ticks ||
        descriptor.decision_ticks > descriptor.maximum_ticks) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest interface tick bounds are invalid",
            }
        );
    }
    if (!descriptor.allow_tick_override &&
        (descriptor.minimum_ticks != descriptor.decision_ticks ||
         descriptor.maximum_ticks != descriptor.decision_ticks)) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "A fixed-tick playtest interface must use its "
                           "decision tick for both bounds",
            }
        );
    }
    return {};
}

} // namespace

std::string_view playtest_error_kind_name(PlaytestErrorKind kind) {
    switch (kind) {
        case PlaytestErrorKind::InvalidAction:
            return "invalid_action";
        case PlaytestErrorKind::Conflict:
            return "conflict";
        case PlaytestErrorKind::Unsupported:
            return "unsupported";
        case PlaytestErrorKind::Internal:
            return "internal";
    }
    return "internal";
}

Status<PlaytestError>
PlaytestRegistry::add(PlaytestInterfaceRegistration registration) {
    if (m_frozen) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Conflict,
                .message = "Playtest registry is frozen",
            }
        );
    }
    if (auto status = validate_descriptor(registration.descriptor); !status) {
        return status;
    }
    auto action_schema = compile_schema(
        "Playtest action schema",
        registration.descriptor.action_schema_json
    );
    if (!action_schema) {
        return failure(std::move(action_schema.error()));
    }
    auto observation_schema = compile_schema(
        "Playtest observation schema",
        registration.descriptor.observation_schema_json
    );
    if (!observation_schema) {
        return failure(std::move(observation_schema.error()));
    }
    if (!registration.begin_step) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::InvalidAction,
                .message = "Playtest interface begin_step must be callable",
            }
        );
    }
    if (!registration.end_step) {
        registration.end_step = [](World&) -> Status<PlaytestError> {
            return {};
        };
    }
    if (!registration.observe) {
        registration.observe =
            [](World&) -> Result<std::string, PlaytestError> {
            return std::string("{}");
        };
    }

    const auto action_schema_ptr =
        std::make_shared<const Json>(std::move(*action_schema));
    auto begin_step = std::move(registration.begin_step);
    registration.begin_step = [schema = action_schema_ptr,
                               callback = std::move(begin_step)](
                                  World& world,
                                  std::string_view action_json
                              ) -> Status<PlaytestError> {
        Json action;
        try {
            action = Json::parse(action_json);
        } catch (const std::exception& error) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::InvalidAction,
                    .message =
                        std::string("Playtest action is not valid JSON: ") +
                        error.what(),
                }
            );
        }
        if (auto error =
                detail::validate_json_schema_instance(*schema, action)) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::InvalidAction,
                    .message = "Playtest action " + std::move(*error),
                }
            );
        }
        return callback(world, action_json);
    };

    const auto observation_schema_ptr =
        std::make_shared<const Json>(std::move(*observation_schema));
    auto observe = std::move(registration.observe);
    registration.observe = [schema = observation_schema_ptr,
                            callback = std::move(observe)](
                               World& world
                           ) -> Result<std::string, PlaytestError> {
        auto result = callback(world);
        if (!result) {
            return failure(std::move(result.error()));
        }
        Json observation;
        try {
            observation = Json::parse(*result);
        } catch (const std::exception& error) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = std::string(
                                   "Playtest observation is not valid JSON: "
                               ) +
                               error.what(),
                }
            );
        }
        if (auto error =
                detail::validate_json_schema_instance(*schema, observation)) {
            return failure(
                PlaytestError {
                    .kind = PlaytestErrorKind::Internal,
                    .message = "Playtest observation " + std::move(*error),
                }
            );
        }
        return result;
    };
    if (m_indices.contains(registration.descriptor.id)) {
        return failure(
            PlaytestError {
                .kind = PlaytestErrorKind::Conflict,
                .message = "Playtest interface '" + registration.descriptor.id +
                           "' is already registered",
            }
        );
    }

    const auto index = m_interfaces.size();
    m_indices.emplace(registration.descriptor.id, index);
    m_interfaces.push_back(std::move(registration));
    return {};
}

const PlaytestInterfaceRegistration*
PlaytestRegistry::find(std::string_view id) const {
    const auto entry = m_indices.find(std::string(id));
    if (entry == m_indices.end()) {
        return nullptr;
    }
    return &m_interfaces[entry->second];
}

} // namespace fei::runtime_protocol
