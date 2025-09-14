#pragma once
#include "base/optional.hpp"
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"

#include <limits>

namespace fei {

struct SamplerDescription {
    SamplerAddressMode address_mode_u {SamplerAddressMode::Repeat};
    SamplerAddressMode address_mode_v {SamplerAddressMode::Repeat};
    SamplerAddressMode address_mode_w {SamplerAddressMode::Repeat};
    SamplerFilter mag_filter {SamplerFilter::Linear};
    SamplerFilter min_filter {SamplerFilter::Linear};
    SamplerFilter mipmap_filter {SamplerFilter::Linear};
    Optional<ComparisonKind> comparison_kind;
    float max_anisotropy {1.0f};
    float min_lod {0.0f};
    float max_lod {std::numeric_limits<float>::max()};
    float lod_bias {0.0f};
    SamplerBorderColor border_color {SamplerBorderColor::TransparentBlack};

    static SamplerDescription Point;
    static SamplerDescription Linear;
    static SamplerDescription Aniso4x;
};

inline SamplerDescription SamplerDescription::Point = {
    SamplerAddressMode::Repeat,
    SamplerAddressMode::Repeat,
    SamplerAddressMode::Repeat,
    SamplerFilter::Nearest,
    SamplerFilter::Nearest,
    SamplerFilter::Nearest,
    {},
    0,
    0,
    std::numeric_limits<decltype(max_lod)>::max(),
    0,
    SamplerBorderColor::TransparentBlack
};

inline SamplerDescription SamplerDescription::Linear = {
    SamplerAddressMode::Repeat,
    SamplerAddressMode::Repeat,
    SamplerAddressMode::Repeat,
    SamplerFilter::Linear,
    SamplerFilter::Linear,
    SamplerFilter::Linear,
    {},
    0,
    0,
    std::numeric_limits<decltype(max_lod)>::max(),
    0,
    SamplerBorderColor::TransparentBlack
};

inline SamplerDescription SamplerDescription::Aniso4x = {
    SamplerAddressMode::Repeat,
    SamplerAddressMode::Repeat,
    SamplerAddressMode::Repeat,
    SamplerFilter::Linear,
    SamplerFilter::Linear,
    SamplerFilter::Linear,
    {},
    4,
    0,
    std::numeric_limits<decltype(max_lod)>::max(),
    0,
    SamplerBorderColor::TransparentBlack
};

class Sampler : public BindableResource {
  public:
    virtual ~Sampler() = default;
};

} // namespace fei
