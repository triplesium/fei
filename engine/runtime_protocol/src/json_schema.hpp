#pragma once

#include "base/result.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace fei::runtime_protocol::detail {

[[nodiscard]] Result<nlohmann::json, std::string>
compile_json_schema(std::string_view source);

[[nodiscard]] std::optional<std::string> validate_json_schema_instance(
    const nlohmann::json& schema,
    const nlohmann::json& instance
);

} // namespace fei::runtime_protocol::detail
