#pragma once
#include "base/optional.hpp"
#include "graphics/texture.hpp"
#include "math/color.hpp"

#include <memory>
#include <vector>

namespace fei {

enum class LoadOp { Load, Clear };

struct RenderPassColorAttachment {
    std::shared_ptr<Texture> texture;
    LoadOp load_op {LoadOp::Clear};
    Color4F clear_color {0.0f, 0.0f, 0.0f, 1.0f};
};

struct RenderPassDepthStencilAttachment {
    std::shared_ptr<Texture> texture;
    LoadOp depth_load_op {LoadOp::Clear};
    LoadOp stencil_load_op {LoadOp::Clear};
    float clear_depth {1.0f};
    std::uint8_t clear_stencil {0};
};

struct RenderPassDescription {
    std::vector<RenderPassColorAttachment> color_attachments;
    Optional<RenderPassDepthStencilAttachment> depth_stencil_attachment;
};

} // namespace fei
