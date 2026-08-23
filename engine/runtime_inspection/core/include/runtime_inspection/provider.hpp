#pragma once

#include "base/result.hpp"
#include "base/types.hpp"

#include <concepts>
#include <string>
#include <string_view>

namespace ets {

class World;

namespace runtime_inspection {

enum class InspectionErrorKind : uint8 {
    InvalidRequest,
    NotFound,
    Conflict,
    Unsupported,
    ResponseTooLarge,
    Internal,
};

enum class InspectionCost : uint8 {
    Low,
    Moderate,
    High,
};

struct InspectionError {
    InspectionErrorKind kind {InspectionErrorKind::Internal};
    std::string message;
};

[[nodiscard]] std::string_view
inspection_error_kind_name(InspectionErrorKind kind);

[[nodiscard]] std::string_view inspection_cost_name(InspectionCost cost);

template<typename Provider>
concept InspectionProvider = requires(
    const Provider& provider,
    World& world,
    const typename Provider::Request& request
) {
    typename Provider::Request;
    typename Provider::Response;
    { Provider::id } -> std::convertible_to<std::string_view>;
    { Provider::label } -> std::convertible_to<std::string_view>;
    { Provider::description } -> std::convertible_to<std::string_view>;
    { Provider::schema } -> std::convertible_to<std::string_view>;
    { Provider::read_only } -> std::convertible_to<bool>;
    { Provider::cost } -> std::convertible_to<InspectionCost>;
    { Provider::request_schema_json } -> std::convertible_to<std::string_view>;
    { Provider::response_schema_json } -> std::convertible_to<std::string_view>;
    {
        provider.inspect(world, request)
    } -> std::same_as<Result<typename Provider::Response, InspectionError>>;
};

} // namespace runtime_inspection
} // namespace ets
