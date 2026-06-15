#pragma once
#include "base/types.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <vector>

namespace fei {

struct WebPreviewCapturedFrame {
    std::vector<byte> jpeg;
    uint32 width {0};
    uint32 height {0};
};

WebPreviewCapturedFrame capture_web_preview_texture(
    const std::shared_ptr<Texture>& texture,
    int jpeg_quality
);

} // namespace fei
