#include "runtime_inspection/registry.hpp"

#include <exception>
#include <string>
#include <utility>

namespace fei::runtime_inspection {
namespace {

Status<InspectionError>
validate_descriptor(const InspectionDescriptor& descriptor) {
    if (descriptor.id.empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Inspection provider ID must not be empty",
            }
        );
    }
    if (descriptor.label.empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Inspection provider label must not be empty",
            }
        );
    }
    if (descriptor.description.empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Inspection provider description must not be empty",
            }
        );
    }
    if (descriptor.schema.empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Inspection provider schema must not be empty",
            }
        );
    }
    if (descriptor.request_schema_json.empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message =
                    "Inspection provider request schema must not be empty",
            }
        );
    }
    if (descriptor.response_schema_json.empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message =
                    "Inspection provider response schema must not be empty",
            }
        );
    }
    return {};
}

} // namespace

Status<InspectionError> InspectionRegistry::add(
    InspectionDescriptor descriptor,
    InspectionHandler handler
) {
    if (m_frozen) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Conflict,
                .message = "Inspection registry is frozen",
            }
        );
    }
    if (auto status = validate_descriptor(descriptor); !status) {
        return status;
    }
    if (!handler) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Inspection provider handler must not be empty",
            }
        );
    }
    if (m_indices.contains(descriptor.id)) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Conflict,
                .message = "Inspection provider '" + descriptor.id +
                           "' is already registered",
            }
        );
    }

    const auto index = m_descriptors.size();
    m_indices.emplace(descriptor.id, index);
    m_descriptors.push_back(std::move(descriptor));
    m_handlers.push_back(std::move(handler));
    return {};
}

Result<std::string, InspectionError> InspectionRegistry::dispatch(
    World& world,
    const InspectionInvocation& invocation
) const {
    if (invocation.provider.empty()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "Inspection provider ID must not be empty",
            }
        );
    }
    const auto entry = m_indices.find(std::string(invocation.provider));
    if (entry == m_indices.end()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "Unknown inspection provider '" +
                           std::string(invocation.provider) + "'",
            }
        );
    }

    const auto index = entry->second;
    const auto& descriptor = m_descriptors[index];
    if (invocation.schema != descriptor.schema) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "Inspection provider '" + descriptor.id +
                           "' supports schema '" + descriptor.schema +
                           "', not '" + std::string(invocation.schema) + "'",
            }
        );
    }

    try {
        return m_handlers[index](world, invocation.payload_json);
    } catch (const std::exception& error) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Internal,
                .message = "Inspection provider '" + descriptor.id +
                           "' failed: " + error.what(),
            }
        );
    } catch (...) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Internal,
                .message = "Inspection provider '" + descriptor.id +
                           "' failed with an unknown exception",
            }
        );
    }
}

bool InspectionRegistry::contains(std::string_view provider) const {
    return m_indices.contains(std::string(provider));
}

} // namespace fei::runtime_inspection
