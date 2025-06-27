#pragma once

#include "graphics/enums.hpp"
#include <cstddef>

namespace fei {
struct SamplerDescriptor {
    SamplerFilter mag_filter {SamplerFilter::Linear};
    SamplerFilter min_filter {SamplerFilter::Linear};
    SamplerAddressMode s_address_mode {SamplerAddressMode::ClampToEdge};
    SamplerAddressMode t_address_mode {SamplerAddressMode::ClampToEdge};
};

struct TextureDescriptor {
    TextureType texture_type {TextureType::Texture2D};
    PixelFormat texture_format {PixelFormat::RGBA8888};
    TextureUsage texture_usage {TextureUsage::Read};
    int width {0};
    int height {0};
    int depth {0};
    SamplerDescriptor sampler_descriptor {};
    const std::byte* data {nullptr};
};

class Texture2D {
  public:
    virtual ~Texture2D() = default;
    virtual void update_data(
        const std::byte* data,
        std::uint32_t width,
        std::uint32_t height
    ) = 0;
    virtual void apply(std::uint32_t index) const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
} // namespace fei
