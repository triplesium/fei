#include "graphics_opengl/command_buffer.hpp"

#include "base/log.hpp"

#include <cstring>
#include <utility>

namespace fei {

namespace ogl_cmd = opengl_commands;

void CommandBufferOpenGL::begin() {
    if (m_state == State::Recording) {
        fatal("CommandBufferOpenGL::begin called while already recording");
    }
    if (m_state == State::Executable) {
        fatal(
            "CommandBufferOpenGL::begin called before submitting the previous "
            "recording"
        );
    }

    m_commands.clear();
    m_pipeline.reset();
    m_state = State::Recording;
}

void CommandBufferOpenGL::end() {
    if (m_state != State::Recording) {
        fatal("CommandBufferOpenGL::end called before begin");
    }

    m_state = State::Executable;
}

void CommandBufferOpenGL::begin_render_pass(const RenderPassDescription& desc) {
    ensure_recording("begin_render_pass");
    m_commands.emplace_back(ogl_cmd::BeginRenderPass {.desc = desc});
}

void CommandBufferOpenGL::end_render_pass() {
    ensure_recording("end_render_pass");
    m_commands.emplace_back(ogl_cmd::EndRenderPass {});
}

void CommandBufferOpenGL::set_viewport(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t w,
    std::uint32_t h
) {
    ensure_recording("set_viewport");
    m_commands.emplace_back(
        ogl_cmd::SetViewport {.x = x, .y = y, .w = w, .h = h}
    );
}

void CommandBufferOpenGL::set_vertex_buffer(
    std::shared_ptr<const Buffer> buffer
) {
    ensure_recording("set_vertex_buffer");
    m_commands.emplace_back(
        ogl_cmd::SetVertexBuffer {.buffer = std::move(buffer)}
    );
}

void CommandBufferOpenGL::set_resource_set(
    uint32 slot,
    std::shared_ptr<const ResourceSet> resource_set,
    std::span<const uint32> dynamic_offsets
) {
    ensure_recording("set_resource_set");
    m_commands.emplace_back(
        ogl_cmd::SetResourceSet {
            .slot = slot,
            .resource_set = std::move(resource_set),
            .dynamic_offsets = std::vector<uint32>(
                dynamic_offsets.begin(),
                dynamic_offsets.end()
            ),
        }
    );
}

void CommandBufferOpenGL::update_buffer(
    std::shared_ptr<Buffer> buffer,
    uint32 offset,
    const void* data,
    std::size_t size
) {
    ensure_recording("update_buffer");
    if (size > 0 && data == nullptr) {
        fatal("CommandBufferOpenGL::update_buffer received null data");
    }

    std::vector<std::byte> bytes(size);
    if (size > 0) {
        std::memcpy(bytes.data(), data, size);
    }

    m_commands.emplace_back(
        ogl_cmd::UpdateBuffer {
            .buffer = std::move(buffer),
            .offset = offset,
            .data = std::move(bytes),
        }
    );
}

void CommandBufferOpenGL::draw(size_t start, size_t count) {
    ensure_recording("draw");
    m_commands.emplace_back(ogl_cmd::Draw {.start = start, .count = count});
}

void CommandBufferOpenGL::draw_indexed(size_t count) {
    ensure_recording("draw_indexed");
    m_commands.emplace_back(ogl_cmd::DrawIndexed {.count = count});
}

void CommandBufferOpenGL::dispatch(
    std::size_t group_x,
    std::size_t group_y,
    std::size_t group_z
) {
    ensure_recording("dispatch");
    m_commands.emplace_back(
        ogl_cmd::Dispatch {
            .group_x = group_x,
            .group_y = group_y,
            .group_z = group_z,
        }
    );
}

void CommandBufferOpenGL::set_render_pipeline_impl(
    std::shared_ptr<const Pipeline> pipeline
) {
    ensure_recording("set_render_pipeline");
    m_commands.emplace_back(
        ogl_cmd::SetRenderPipeline {.pipeline = std::move(pipeline)}
    );
}

void CommandBufferOpenGL::set_compute_pipeline_impl(
    std::shared_ptr<const Pipeline> pipeline
) {
    ensure_recording("set_compute_pipeline");
    m_commands.emplace_back(
        ogl_cmd::SetComputePipeline {.pipeline = std::move(pipeline)}
    );
}

void CommandBufferOpenGL::set_index_buffer_impl(
    std::shared_ptr<const Buffer> buffer,
    IndexFormat format,
    uint32 offset
) {
    ensure_recording("set_index_buffer");
    m_commands.emplace_back(
        ogl_cmd::SetIndexBuffer {
            .buffer = std::move(buffer),
            .format = format,
            .offset = offset,
        }
    );
}

void CommandBufferOpenGL::generate_mipmaps_impl(
    std::shared_ptr<const Texture> texture
) {
    ensure_recording("generate_mipmaps");
    m_commands.emplace_back(
        ogl_cmd::GenerateMipmaps {.texture = std::move(texture)}
    );
}

void CommandBufferOpenGL::copy_texture_impl(
    std::shared_ptr<const Texture> src,
    uint32 src_x,
    uint32 src_y,
    uint32 src_z,
    uint32 src_mip_level,
    uint32 src_base_array_layer,
    std::shared_ptr<const Texture> dst,
    uint32 dst_x,
    uint32 dst_y,
    uint32 dst_z,
    uint32 dst_mip_level,
    uint32 dst_base_array_layer,
    uint32 width,
    uint32 height,
    uint32 depth,
    uint32 layer_count
) {
    ensure_recording("copy_texture");
    m_commands.emplace_back(
        ogl_cmd::CopyTexture {
            .src = std::move(src),
            .src_x = src_x,
            .src_y = src_y,
            .src_z = src_z,
            .src_mip_level = src_mip_level,
            .src_base_array_layer = src_base_array_layer,
            .dst = std::move(dst),
            .dst_x = dst_x,
            .dst_y = dst_y,
            .dst_z = dst_z,
            .dst_mip_level = dst_mip_level,
            .dst_base_array_layer = dst_base_array_layer,
            .width = width,
            .height = height,
            .depth = depth,
            .layer_count = layer_count,
        }
    );
}

void CommandBufferOpenGL::ensure_recording(const char* command_name) const {
    if (m_state != State::Recording) {
        fatal("CommandBufferOpenGL::{} called outside begin/end", command_name);
    }
}

void CommandBufferOpenGL::ensure_executable(const char* operation_name) const {
    if (m_state != State::Executable) {
        fatal("CommandBufferOpenGL::{} called before end", operation_name);
    }
}

void CommandBufferOpenGL::mark_submitted() {
    m_state = State::Submitted;
}

} // namespace fei
