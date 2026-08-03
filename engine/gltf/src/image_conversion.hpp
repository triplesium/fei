#pragma once

#include "base/result.hpp"

#include <fastgltf/types.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace fei {
class Image;
}

namespace fei::gltf_detail {

Result<std::vector<std::unique_ptr<Image>>, std::string>
convert_textures(
    const fastgltf::Asset& asset,
    std::span<const std::uint8_t> srgb_textures
);

} // namespace fei::gltf_detail
