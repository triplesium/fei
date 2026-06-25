#pragma once

#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/mapped_resource.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture.hpp"
#include "graphics/texture_readback.hpp"
#include "graphics/texture_view.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace fei::rendering_test {

class FakePipeline : public Pipeline {};

class FakeBuffer : public Buffer {
  public:
    explicit FakeBuffer(BufferDescription desc) : m_desc(desc) {}

    std::size_t size() const override { return m_desc.size; }
    BitFlags<BufferUsages> usages() const override { return m_desc.usages; }

  private:
    BufferDescription m_desc;
};

class FakeTexture : public Texture {
  public:
    explicit FakeTexture(TextureDescription desc) : m_desc(desc) {}

    PixelFormat format() const override { return m_desc.texture_format; }
    uint32 width() const override { return m_desc.width; }
    uint32 height() const override { return m_desc.height; }
    uint32 depth() const override { return m_desc.depth; }
    uint32 mip_level() const override { return m_desc.mip_level; }
    uint32 layer() const override { return m_desc.layer; }
    BitFlags<TextureUsage> usage() const override {
        return m_desc.texture_usage;
    }
    TextureType type() const override { return m_desc.texture_type; }

  private:
    TextureDescription m_desc;
};

struct TextureUpdateCall {
    std::shared_ptr<Texture> texture;
    const void* source_data {nullptr};
    std::vector<std::byte> bytes;
    std::uint32_t x {0};
    std::uint32_t y {0};
    std::uint32_t z {0};
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::uint32_t depth {0};
    std::uint32_t mip_level {0};
    std::uint32_t layer {0};
};

struct BufferUpdateCall {
    std::shared_ptr<Buffer> buffer;
    std::uint32_t offset {0};
    std::vector<std::byte> bytes;
};

class FakeGraphicsDevice : public GraphicsDevice {
  public:
    mutable std::vector<ShaderDescription> shader_descriptions;
    mutable std::vector<BufferDescription> buffer_descriptions;
    mutable std::vector<TextureDescription> texture_descriptions;
    mutable std::vector<TextureViewDescription> texture_view_descriptions;
    mutable std::vector<RenderPipelineDescription> render_pipeline_descriptions;
    mutable std::vector<ComputePipelineDescription>
        compute_pipeline_descriptions;
    mutable std::vector<ResourceLayoutDescription> resource_layout_descriptions;
    mutable std::vector<ResourceSetDescription> resource_set_descriptions;
    mutable std::vector<SamplerDescription> sampler_descriptions;
    mutable std::vector<TextureUpdateCall> texture_update_calls;
    mutable std::vector<BufferUpdateCall> buffer_update_calls;

    mutable std::vector<std::shared_ptr<FakeBuffer>> buffers;
    mutable std::vector<std::shared_ptr<FakeTexture>> textures;
    mutable std::vector<std::shared_ptr<Pipeline>> render_pipelines;
    mutable std::vector<std::shared_ptr<Pipeline>> compute_pipelines;
    bool fail_render_pipeline_creation {false};
    bool fail_compute_pipeline_creation {false};

    std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) const override {
        shader_descriptions.push_back(desc);
        return std::make_shared<ShaderModule>(desc);
    }

    std::shared_ptr<Buffer>
    create_buffer(const BufferDescription& desc) const override {
        buffer_descriptions.push_back(desc);
        auto buffer = std::make_shared<FakeBuffer>(desc);
        buffers.push_back(buffer);
        return buffer;
    }

    std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) const override {
        texture_descriptions.push_back(desc);
        auto texture = std::make_shared<FakeTexture>(desc);
        textures.push_back(texture);
        return texture;
    }

    std::shared_ptr<TextureView>
    create_texture_view(const TextureViewDescription& desc) const override {
        texture_view_descriptions.push_back(desc);
        return std::make_shared<TextureView>(desc);
    }

    std::shared_ptr<CommandBuffer> create_command_buffer() const override {
        return nullptr;
    }

    std::shared_ptr<Pipeline> create_render_pipeline(
        const RenderPipelineDescription& desc
    ) const override {
        render_pipeline_descriptions.push_back(desc);
        if (fail_render_pipeline_creation) {
            return nullptr;
        }
        auto pipeline = std::make_shared<FakePipeline>();
        render_pipelines.push_back(pipeline);
        return pipeline;
    }

    std::shared_ptr<Pipeline> create_compute_pipeline(
        const ComputePipelineDescription& desc
    ) const override {
        compute_pipeline_descriptions.push_back(desc);
        if (fail_compute_pipeline_creation) {
            return nullptr;
        }
        auto pipeline = std::make_shared<FakePipeline>();
        compute_pipelines.push_back(pipeline);
        return pipeline;
    }

    std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) const override {
        return std::make_shared<Framebuffer>(desc);
    }

    std::shared_ptr<ResourceLayout> create_resource_layout(
        const ResourceLayoutDescription& desc
    ) const override {
        resource_layout_descriptions.push_back(desc);
        return std::make_shared<ResourceLayout>(desc);
    }

    std::shared_ptr<ResourceSet>
    create_resource_set(const ResourceSetDescription& desc) const override {
        resource_set_descriptions.push_back(desc);
        return std::make_shared<ResourceSet>(desc);
    }

    std::shared_ptr<Sampler>
    create_sampler(const SamplerDescription& desc) const override {
        sampler_descriptions.push_back(desc);
        return std::make_shared<Sampler>();
    }

    void submit_commands(std::shared_ptr<CommandBuffer>) const override {}

    void update_texture(
        std::shared_ptr<Texture> texture,
        const void* data,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t depth,
        std::uint32_t mip_level,
        std::uint32_t layer
    ) const override {
        TextureUpdateCall call {
            .texture = std::move(texture),
            .source_data = data,
            .x = x,
            .y = y,
            .z = z,
            .width = width,
            .height = height,
            .depth = depth,
            .mip_level = mip_level,
            .layer = layer,
        };

        const auto byte_count = static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) *
                                static_cast<std::size_t>(depth) *
                                get_pixel_format_size(call.texture->format());
        if (data != nullptr && byte_count > 0) {
            const auto* bytes = static_cast<const std::byte*>(data);
            call.bytes.assign(bytes, bytes + byte_count);
        }
        texture_update_calls.push_back(std::move(call));
    }

    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) const override {
        BufferUpdateCall call {
            .buffer = std::move(buffer),
            .offset = offset,
        };
        if (data != nullptr && size > 0) {
            const auto* bytes = static_cast<const std::byte*>(data);
            call.bytes.assign(bytes, bytes + size);
        }
        buffer_update_calls.push_back(std::move(call));
    }

    MappedResource
    map(std::shared_ptr<MappableResource>, MapMode map_mode) const override {
        return MappedResource(nullptr, map_mode, std::span<std::byte> {});
    }

    void unmap(std::shared_ptr<MappableResource>) const override {}

    std::shared_ptr<TextureReadback>
    create_texture_readback(uint32 = 3) const override {
        return nullptr;
    }

    std::shared_ptr<const Framebuffer> main_framebuffer() const override {
        return nullptr;
    }
};

} // namespace fei::rendering_test
