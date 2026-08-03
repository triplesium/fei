#pragma once
#include "base/optional.hpp"
#include "base/types.hpp"
#include "graphics/texture.hpp"
#include "math/color.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace fei {

class Framebuffer;

enum class LoadOp : uint8 { Load, Clear, DontCare };
enum class StoreOp : uint8 { Store, DontCare };

struct RenderPassColorAttachment {
    std::shared_ptr<const Texture> texture;
    LoadOp load_op {LoadOp::Clear};
    Color4F clear_color {0.0f, 0.0f, 0.0f, 1.0f};
    StoreOp store_op {StoreOp::Store};
};

struct RenderPassDepthStencilAttachment {
    std::shared_ptr<const Texture> texture;
    LoadOp depth_load_op {LoadOp::Clear};
    LoadOp stencil_load_op {LoadOp::Clear};
    float clear_depth {1.0f};
    std::uint8_t clear_stencil {0};
    StoreOp depth_store_op {StoreOp::Store};
    StoreOp stencil_store_op {StoreOp::Store};
};

struct RenderPassDescription {
    std::vector<RenderPassColorAttachment> color_attachments;
    Optional<RenderPassDepthStencilAttachment> depth_stencil_attachment;
    std::shared_ptr<const Framebuffer> framebuffer;
};

} // namespace fei
