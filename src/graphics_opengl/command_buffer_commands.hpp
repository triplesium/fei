#pragma once

#include "base/types.hpp"
#include "graphics/buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/render_pass.hpp"
#include "graphics/resource.hpp"
#include "graphics/texture.hpp"
#include "math/color.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace fei::opengl_commands {

struct BeginRenderPass {
    RenderPassDescription desc;
};
struct EndRenderPass {};
struct SetFramebuffer {
    std::shared_ptr<const Framebuffer> framebuffer;
};
struct SetViewport {
    std::int32_t x;
    std::int32_t y;
    std::uint32_t w;
    std::uint32_t h;
};
struct ClearColor {
    Color4F color;
};
struct ClearDepth {
    float depth;
};
struct ClearStencil {
    std::uint8_t stencil;
};
struct SetRenderPipeline {
    std::shared_ptr<const Pipeline> pipeline;
};
struct SetComputePipeline {
    std::shared_ptr<const Pipeline> pipeline;
};
struct SetVertexBuffer {
    std::shared_ptr<const Buffer> buffer;
};
struct SetIndexBuffer {
    std::shared_ptr<const Buffer> buffer;
    IndexFormat format;
    uint32 offset;
};
struct SetResourceSet {
    uint32 slot;
    std::shared_ptr<const ResourceSet> resource_set;
};
struct UpdateBuffer {
    std::shared_ptr<Buffer> buffer;
    std::vector<std::byte> data;
};
struct Draw {
    std::size_t start;
    std::size_t count;
};
struct DrawIndexed {
    std::size_t count;
};
struct Dispatch {
    std::size_t group_x;
    std::size_t group_y;
    std::size_t group_z;
};
struct BlitTo {
    std::shared_ptr<const Framebuffer> target;
};
struct GenerateMipmaps {
    std::shared_ptr<const Texture> texture;
};
struct CopyTexture {
    std::shared_ptr<const Texture> src;
    uint32 src_x;
    uint32 src_y;
    uint32 src_z;
    uint32 src_mip_level;
    uint32 src_base_array_layer;
    std::shared_ptr<const Texture> dst;
    uint32 dst_x;
    uint32 dst_y;
    uint32 dst_z;
    uint32 dst_mip_level;
    uint32 dst_base_array_layer;
    uint32 width;
    uint32 height;
    uint32 depth;
    uint32 layer_count;
};

using Command = std::variant<
    BeginRenderPass,
    EndRenderPass,
    SetFramebuffer,
    SetViewport,
    ClearColor,
    ClearDepth,
    ClearStencil,
    SetRenderPipeline,
    SetComputePipeline,
    SetVertexBuffer,
    SetIndexBuffer,
    SetResourceSet,
    UpdateBuffer,
    Draw,
    DrawIndexed,
    Dispatch,
    BlitTo,
    GenerateMipmaps,
    CopyTexture>;

} // namespace fei::opengl_commands
