#pragma once

#include "base/result.hpp"

#include <fastgltf/types.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ets {
class Image;
}

namespace ets::gltf_detail {

Result<std::vector<std::unique_ptr<Image>>, std::string>
convert_textures(
    const fastgltf::Asset& asset,
    std::span<const std::uint8_t> srgb_textures
);

} // namespace ets::gltf_detail
