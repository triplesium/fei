#pragma once
#include "graphics/enums.hpp"

#include <cstdint>

namespace fei {

struct SamplerDescription {
    SamplerFilter mag_filter {SamplerFilter::Linear};
    SamplerFilter min_filter {SamplerFilter::Linear};
    SamplerAddressMode s_address_mode {SamplerAddressMode::ClampToEdge};
    SamplerAddressMode t_address_mode {SamplerAddressMode::ClampToEdge};
};

struct TextureDescription {
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::uint32_t depth {0};
    std::uint32_t mip_level {0};
    std::uint32_t layer {0};
    PixelFormat texture_format {PixelFormat::RGBA8888};
    TextureUsage texture_usage {TextureUsage::Read};
    TextureType texture_type {TextureType::Texture2D};
    SamplerDescription sampler_descriptor {};
};

class Texture {
  public:
    virtual ~Texture() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
} // namespace fei
