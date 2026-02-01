#pragma once
#include "base/bitflags.hpp"
#include "base/types.hpp"
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"

namespace fei {

struct TextureDescription {
    uint32 width {0};
    uint32 height {0};
    uint32 depth {0};
    uint32 mip_level {1};
    uint32 layer {1};
    PixelFormat texture_format;
    BitFlags<TextureUsage> texture_usage;
    TextureType texture_type;
};

class Texture : public BindableResource, public MappableResource {
  public:
    virtual ~Texture() = default;
    virtual PixelFormat format() const = 0;
    virtual uint32 width() const = 0;
    virtual uint32 height() const = 0;
    virtual uint32 depth() const = 0;
    virtual uint32 mip_level() const = 0;
    virtual uint32 layer() const = 0;
    virtual BitFlags<TextureUsage> usage() const = 0;
    virtual TextureType type() const = 0;
};
} // namespace fei
