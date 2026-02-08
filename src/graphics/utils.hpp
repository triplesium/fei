#pragma once
#include "base/types.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <tuple>

namespace fei {

class Utils {
  public:
    static uint32
    get_dimension(uint32 largest_level_dimension, uint32 mip_level) {
        return std::max(largest_level_dimension >> mip_level, 1u);
    }

    static std::tuple<uint32, uint32, uint32>
    get_mip_dimensions(std::shared_ptr<Texture> texture, uint32 mip_level) {
        uint32 width = get_dimension(texture->width(), mip_level);
        uint32 height = get_dimension(texture->height(), mip_level);
        uint32 depth = get_dimension(texture->depth(), mip_level);
        return {width, height, depth};
    }
};

} // namespace fei
