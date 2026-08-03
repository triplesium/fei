#pragma once
#include "base/bitflags.hpp"
#include "base/types.hpp"
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"

#include <memory>
#include <mutex>

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
    TextureSampleCount sample_count {TextureSampleCount::Count1};
};

class TextureView;
class GraphicsDevice;

class Texture : public BindableResource,
                public MappableResource,
                public std::enable_shared_from_this<Texture> {
  private:
    mutable std::shared_ptr<TextureView> m_full_view;
    mutable std::mutex m_full_view_mutex;

  public:
    ~Texture() override = default;
    virtual PixelFormat format() const = 0;
    virtual uint32 width() const = 0;
    virtual uint32 height() const = 0;
    virtual uint32 depth() const = 0;
    virtual uint32 mip_level() const = 0;
    virtual uint32 layer() const = 0;
    virtual BitFlags<TextureUsage> usage() const = 0;
    virtual TextureType type() const = 0;
    virtual TextureSampleCount sample_count() const = 0;
    std::shared_ptr<const TextureView>
    full_view(const GraphicsDevice& device) const;
};
} // namespace fei
