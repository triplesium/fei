#pragma once
#include "graphics/command_buffer.hpp"
#include "graphics/opengl/graphics_device.hpp"
#include "graphics/opengl/resource.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/resource.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace fei {

class CommandBufferOpenGL : public CommandBuffer {
  private:
    std::vector<std::shared_ptr<ResourceSetOpenGL>> m_bound_resource_sets;
    GraphicsDeviceOpenGL& m_device;
    GLenum m_draw_elements_type;

  public:
    CommandBufferOpenGL(GraphicsDeviceOpenGL& device) : m_device(device) {}
    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassDescription& desc) override;
    void end_render_pass() override;

    void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) override;
    void clear_color(const Color4F& color) override;
    void clear_depth(float depth) override;
    void clear_stencil(std::uint8_t stencil) override;
    void set_vertex_buffer(std::shared_ptr<Buffer> buffer) override;
    void set_resource_set(
        uint32 slot,
        std::shared_ptr<ResourceSet> resource_set
    ) override;
    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) override;
    void draw(std::size_t start, std::size_t count) override;
    void draw_indexed(std::size_t count) override;
    void dispatch(
        std::size_t group_x,
        std::size_t group_y,
        std::size_t group_z
    ) override;

    void blit_to(std::shared_ptr<Framebuffer> target) override;

  protected:
    void
    set_framebuffer_impl(std::shared_ptr<Framebuffer> framebuffer) override;
    void set_render_pipeline_impl(std::shared_ptr<Pipeline> pipeline) override;
    void set_compute_pipeline_impl(std::shared_ptr<Pipeline> pipeline) override;
    void set_index_buffer_impl(
        std::shared_ptr<Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) override;
    void generate_mipmaps_impl(std::shared_ptr<Texture> texture) override;
    void copy_texture_impl(
        std::shared_ptr<Texture> src,
        uint32 src_x,
        uint32 src_y,
        uint32 src_z,
        uint32 src_mip_level,
        uint32 src_base_array_layer,
        std::shared_ptr<Texture> dst,
        uint32 dst_x,
        uint32 dst_y,
        uint32 dst_z,
        uint32 dst_mip_level,
        uint32 dst_base_array_layer,
        uint32 width,
        uint32 height,
        uint32 depth,
        uint32 layer_count
    ) override;

  private:
    uint32 calculate_uniform_block_base_index(uint32 slot);
    uint32 calculate_storage_buffer_base_index(uint32 slot);
};

} // namespace fei
