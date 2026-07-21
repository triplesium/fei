#pragma once

#include "refl/reflect.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fei::devtools::scripting_lua {

inline constexpr std::size_t c_default_max_source_bytes =
    std::size_t {64} * 1024;
inline constexpr std::size_t c_default_max_output_bytes =
    std::size_t {64} * 1024;
inline constexpr std::uint64_t c_default_instruction_limit = 1'000'000;
inline constexpr std::uint32_t c_default_time_limit_ms = 100;

struct FEI_REFLECT EvalRequest {
    std::string source;
};

struct FEI_REFLECT EvalResponse {
    bool ok {false};
    std::vector<std::string> output;
    std::string error;
    bool truncated {false};
};

struct EvalLimits {
    std::size_t max_source_bytes {c_default_max_source_bytes};
    std::size_t max_output_bytes {c_default_max_output_bytes};
    std::uint64_t instruction_limit {c_default_instruction_limit};
    std::uint32_t time_limit_ms {c_default_time_limit_ms};
};

} // namespace fei::devtools::scripting_lua
