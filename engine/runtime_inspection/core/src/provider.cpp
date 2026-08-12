#include "runtime_inspection/provider.hpp"

namespace fei::runtime_inspection {

std::string_view inspection_error_kind_name(InspectionErrorKind kind) {
    switch (kind) {
        case InspectionErrorKind::InvalidRequest:
            return "invalid_request";
        case InspectionErrorKind::NotFound:
            return "not_found";
        case InspectionErrorKind::Conflict:
            return "conflict";
        case InspectionErrorKind::Unsupported:
            return "unsupported";
        case InspectionErrorKind::ResponseTooLarge:
            return "response_too_large";
        case InspectionErrorKind::Internal:
            return "internal";
    }
    return "unknown";
}

std::string_view inspection_cost_name(InspectionCost cost) {
    switch (cost) {
        case InspectionCost::Low:
            return "low";
        case InspectionCost::Moderate:
            return "moderate";
        case InspectionCost::High:
            return "high";
    }
    return "unknown";
}

} // namespace fei::runtime_inspection
