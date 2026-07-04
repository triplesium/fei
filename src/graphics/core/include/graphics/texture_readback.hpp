#pragma once

#include "base/optional.hpp"
#include "base/types.hpp"
#include "graphics/enums.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <vector>

namespace fei {

struct TextureReadbackRequest {
    std::shared_ptr<Texture> texture;
    uint32 mip_level {0};
    uint32 layer {0};
    PixelFormat output_format {PixelFormat::Rgba8Unorm};
    uint64 user_data {0};
};

struct TextureReadbackFrame {
    std::vector<byte> data;
    uint32 width {0};
    uint32 height {0};
    uint32 depth {0};
    PixelFormat format {PixelFormat::Rgba8Unorm};
    uint64 user_data {0};

    bool empty() const { return data.empty(); }
};

class TextureReadback {
  public:
    virtual ~TextureReadback() = default;

    virtual bool can_enqueue() const = 0;
    virtual bool enqueue(TextureReadbackRequest request) = 0;
    virtual Optional<TextureReadbackFrame> poll() = 0;
    virtual void reset() = 0;
};

} // namespace fei
