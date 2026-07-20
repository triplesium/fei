#pragma once

#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

#include <cstddef>
#include <string>

namespace fei {

class World;

namespace devtools::ecs {

inline constexpr std::size_t c_max_entity_inspect_response_bytes =
    std::size_t {4} * 1024 * 1024;

struct FEI_REFLECT EntityInspectRequest {
    Entity entity {};
};

struct EntityInspectError {
    int status {500};
    std::string message;
};

Result<std::string, EntityInspectError>
inspect_entity(const World& world, const EntityInspectRequest& request);

} // namespace devtools::ecs

} // namespace fei
