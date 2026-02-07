#pragma once
#include "base/optional.hpp"
#include "base/types.hpp"
#include "graphics/resource.hpp"
#include "graphics/texture.hpp"

#include <memory>

namespace fei {

struct TextureViewDescription {
    std::shared_ptr<Texture> target;
    uint32 base_mip_level {0};
    uint32 mip_levels {1};
    uint32 base_array_layer {0};
    uint32 array_layers {1};
    Optional<PixelFormat> format;
};

class TextureView : public BindableResource {
  private:
    std::shared_ptr<Texture> m_target;
    uint32 m_base_mip_level;
    uint32 m_mip_levels;
    uint32 m_base_array_layer;
    uint32 m_array_layers;
    PixelFormat m_format;

  public:
    TextureView(const TextureViewDescription& desc) :
        m_target(desc.target), m_base_mip_level(desc.base_mip_level),
        m_mip_levels(desc.mip_levels),
        m_base_array_layer(desc.base_array_layer),
        m_array_layers(desc.array_layers),
        m_format(desc.format.value_or(desc.target->format())) {}

    std::shared_ptr<Texture> target() const { return m_target; }
    uint32 base_mip_level() const { return m_base_mip_level; }
    uint32 mip_levels() const { return m_mip_levels; }
    uint32 base_array_layer() const { return m_base_array_layer; }
    uint32 array_layers() const { return m_array_layers; }
    PixelFormat format() const { return m_format; }
};

} // namespace fei
