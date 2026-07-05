#pragma once
#include "base/log.hpp"
#include "base/types.hpp"
#include "graphics/buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/render_pass.hpp"
#include "graphics/resource.hpp"
#include "graphics/texture.hpp"
#include "graphics/utils.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace fei {

class CommandBuffer {
  protected:
    std::shared_ptr<const Pipeline> m_pipeline;

  public:
    virtual ~CommandBuffer() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void begin_render_pass(const RenderPassDescription& desc) = 0;
    virtual void end_render_pass() = 0;

    virtual void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) = 0;
    void set_render_pipeline(std::shared_ptr<const Pipeline> pipeline) {
        m_pipeline = pipeline;
        set_render_pipeline_impl(pipeline);
    }
    void set_compute_pipeline(std::shared_ptr<const Pipeline> pipeline) {
        m_pipeline = pipeline;
        set_compute_pipeline_impl(pipeline);
    }
    virtual void set_vertex_buffer(std::shared_ptr<const Buffer> buffer) = 0;

    virtual void
    set_index_buffer(std::shared_ptr<const Buffer> buffer, IndexFormat format) {
        set_index_buffer_impl(buffer, format, 0);
    }

    virtual void set_index_buffer(
        std::shared_ptr<const Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) {
        set_index_buffer_impl(buffer, format, offset);
    }

    void set_resource_set(
        uint32 slot,
        std::shared_ptr<const ResourceSet> resource_set
    ) {
        set_resource_set(slot, std::move(resource_set), {});
    }

    virtual void set_resource_set(
        uint32 slot,
        std::shared_ptr<const ResourceSet> resource_set,
        std::span<const uint32> dynamic_offsets
    ) = 0;
    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) = 0;
    virtual void draw(std::size_t start, std::size_t count) = 0;
    virtual void draw_indexed(std::size_t count) = 0;
    virtual void
    dispatch(std::size_t group_x, std::size_t group_y, std::size_t group_z) = 0;

    void generate_mipmaps(std::shared_ptr<const Texture> texture) {
        if (!texture->usage().is_set(TextureUsage::GenerateMipmaps)) {
            error("Texture does not have GenerateMipmaps usage flag set");
            return;
        }
        generate_mipmaps_impl(texture);
    }

    void copy_texture(
        std::shared_ptr<const Texture> src,
        std::shared_ptr<const Texture> dst
    ) {
        uint32 effective_src_array_layers =
            src->usage().is_set(TextureUsage::Cubemap) ? src->layer() * 6 :
                                                         src->layer();
        for (uint32 level = 0; level < src->mip_level(); level++) {
            auto [width, height, depth] = Utils::get_mip_dimensions(src, level);
            copy_texture(
                src,
                0,
                0,
                0,
                level,
                0,
                dst,
                0,
                0,
                0,
                level,
                0,
                width,
                height,
                depth,
                effective_src_array_layers
            );
        }
    }

    void copy_texture(
        std::shared_ptr<const Texture> src,
        std::shared_ptr<const Texture> dst,
        uint32 mip_level,
        uint32 array_layer
    ) {
        auto [width, height, depth] = Utils::get_mip_dimensions(src, mip_level);
        copy_texture_impl(
            src,
            0,
            0,
            0,
            mip_level,
            array_layer,
            dst,
            0,
            0,
            0,
            mip_level,
            array_layer,
            width,
            height,
            depth,
            1
        );
    }

    void copy_texture(
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
        copy_texture_impl(
            src,
            src_x,
            src_y,
            src_z,
            src_mip_level,
            src_base_array_layer,
            dst,
            dst_x,
            dst_y,
            dst_z,
            dst_mip_level,
            dst_base_array_layer,
            width,
            height,
            depth,
            layer_count
        );
    }

  protected:
    virtual void
    set_render_pipeline_impl(std::shared_ptr<const Pipeline> pipeline) = 0;
    virtual void
    set_compute_pipeline_impl(std::shared_ptr<const Pipeline> pipeline) = 0;
    virtual void set_index_buffer_impl(
        std::shared_ptr<const Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) = 0;
    virtual void
    generate_mipmaps_impl(std::shared_ptr<const Texture> texture) = 0;

    virtual void copy_texture_impl(
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
    ) = 0;
};

} // namespace fei
