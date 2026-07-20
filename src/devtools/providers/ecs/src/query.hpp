#pragma once

#include "base/result.hpp"
#include "base/types.hpp"
#include "refl/reflect.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fei {

class World;

namespace devtools::ecs {

inline constexpr uint32 c_default_query_limit = 50;
inline constexpr uint32 c_max_query_limit = 200;
inline constexpr std::size_t c_max_query_components = 32;
inline constexpr std::size_t c_max_query_selectors = 64;
inline constexpr std::size_t c_max_type_selector_length = 256;
inline constexpr std::size_t c_max_query_response_bytes =
    std::size_t {4} * 1024 * 1024;

struct FEI_REFLECT QueryRequest {
    std::vector<std::string> components;
    std::vector<std::string> with;
    std::vector<std::string> without;
    uint32 limit {c_default_query_limit};
};

struct QueryError {
    int status {500};
    std::string message;
};

Result<std::string, QueryError>
execute_query(World& world, const QueryRequest& request);

} // namespace devtools::ecs

} // namespace fei
