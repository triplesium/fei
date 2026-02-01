#pragma once
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace fei {

class MappedResource {
  private:
    std::shared_ptr<MappableResource> m_resource;
    MapMode m_map_mode;
    std::span<std::byte> m_data;

  public:
    MappedResource(
        std::shared_ptr<MappableResource> resource,
        MapMode map_mode,
        std::span<std::byte> data
    ) : m_resource(resource), m_map_mode(map_mode), m_data(data) {}

    std::shared_ptr<MappableResource> resource() const { return m_resource; }
    MapMode map_mode() const { return m_map_mode; }
    std::span<std::byte> data() const { return m_data; }
};

} // namespace fei
