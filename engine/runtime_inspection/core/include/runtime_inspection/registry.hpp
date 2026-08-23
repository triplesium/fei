#pragma once

#include "base/result.hpp"
#include "runtime_inspection/provider.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets {

class World;

namespace runtime_inspection {

struct InspectionDescriptor {
    std::string id;
    std::string label;
    std::string description;
    std::string schema;
    bool read_only {true};
    InspectionCost cost {InspectionCost::Low};
    std::string request_schema_json;
    std::string response_schema_json;
};

struct InspectionInvocation {
    std::string_view provider;
    std::string_view schema;
    std::string_view payload_json;
};

using InspectionHandler = std::function<
    Result<std::string, InspectionError>(World&, std::string_view)>;

class InspectionRegistry {
  public:
    Status<InspectionError>
    add(InspectionDescriptor descriptor, InspectionHandler handler);

    template<InspectionProvider Provider>
    Status<InspectionError> add(InspectionHandler handler) {
        return add(
            InspectionDescriptor {
                .id = std::string(Provider::id),
                .label = std::string(Provider::label),
                .description = std::string(Provider::description),
                .schema = std::string(Provider::schema),
                .read_only = Provider::read_only,
                .cost = Provider::cost,
                .request_schema_json =
                    std::string(Provider::request_schema_json),
                .response_schema_json =
                    std::string(Provider::response_schema_json),
            },
            std::move(handler)
        );
    }

    [[nodiscard]] Result<std::string, InspectionError>
    dispatch(World& world, const InspectionInvocation& invocation) const;

    [[nodiscard]] std::span<const InspectionDescriptor> descriptors() const {
        return m_descriptors;
    }

    [[nodiscard]] bool contains(std::string_view provider) const;

    void freeze() noexcept { m_frozen = true; }
    [[nodiscard]] bool frozen() const noexcept { return m_frozen; }

  private:
    std::vector<InspectionDescriptor> m_descriptors;
    std::vector<InspectionHandler> m_handlers;
    std::unordered_map<std::string, std::size_t> m_indices;
    bool m_frozen {false};
};

} // namespace runtime_inspection
} // namespace ets
