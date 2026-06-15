#pragma once
#include "base/types.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <string>
#include <vector>

namespace fei {

struct WebPreviewCapturedFrame {
    std::vector<byte> rgba;
    uint32 width {0};
    uint32 height {0};
    std::string error;
};

WebPreviewCapturedFrame
capture_web_preview_texture(const std::shared_ptr<Texture>& texture);

} // namespace fei
