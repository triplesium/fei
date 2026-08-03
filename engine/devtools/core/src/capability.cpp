#include "devtools/capability.hpp"

#include "ecs/world.hpp"

#include <utility>

namespace fei::devtools {

namespace {

template<typename T>
Entity declare_capability_impl(
    World& world,
    std::string id,
    std::string label,
    T desc
) {
    auto entity = world.entity();
    world.add_component(
        entity,
        Capability {.id = std::move(id), .label = std::move(label)}
    );
    world.add_component(entity, std::move(desc));
    return entity;
}

} // namespace

Entity declare_capability(
    World& world,
    std::string id,
    std::string label,
    BlobProtocol protocol
) {
    return declare_capability_impl(
        world,
        std::move(id),
        std::move(label),
        std::move(protocol)
    );
}

Entity declare_capability(
    World& world,
    std::string id,
    std::string label,
    JsonProtocol protocol
) {
    return declare_capability_impl(
        world,
        std::move(id),
        std::move(label),
        std::move(protocol)
    );
}

} // namespace fei::devtools
