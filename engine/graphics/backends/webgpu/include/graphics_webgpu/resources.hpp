#pragma once

#include "graphics/buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture_view.hpp"
#include "graphics_webgpu/context.hpp"

#include <memory>
#include <string>
#include <webgpu/webgpu.h>

namespace fei {

class BufferWebGpu final : public Buffer {
  public:
    BufferWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const BufferDescription& desc
    );
    ~BufferWebGpu() override;

    [[nodiscard]] std::size_t size() const override { return m_desc.size; }
    [[nodiscard]] BitFlags<BufferUsages> usages() const override {
        return m_desc.usages;
    }
    [[nodiscard]] WGPUBuffer handle() const { return m_buffer; }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    BufferDescription m_desc;
    WGPUBuffer m_buffer {nullptr};
};

class TextureWebGpu final : public Texture {
  public:
    TextureWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const TextureDescription& desc
    );
    // Wraps a surface-owned texture handle that is consumed by present.
    TextureWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const TextureDescription& desc,
        WGPUTexture texture
    );
    ~TextureWebGpu() override;

    [[nodiscard]] PixelFormat format() const override {
        return m_desc.texture_format;
    }
    [[nodiscard]] uint32 width() const override { return m_desc.width; }
    [[nodiscard]] uint32 height() const override { return m_desc.height; }
    [[nodiscard]] uint32 depth() const override { return m_desc.depth; }
    [[nodiscard]] uint32 mip_level() const override { return m_desc.mip_level; }
    [[nodiscard]] uint32 layer() const override { return m_desc.layer; }
    [[nodiscard]] BitFlags<TextureUsage> usage() const override {
        return m_desc.texture_usage;
    }
    [[nodiscard]] TextureType type() const override {
        return m_desc.texture_type;
    }
    [[nodiscard]] TextureSampleCount sample_count() const override {
        return m_desc.sample_count;
    }
    [[nodiscard]] WGPUTexture handle() const { return m_texture; }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    TextureDescription m_desc;
    WGPUTexture m_texture {nullptr};
    bool m_release_on_destroy {true};
};

class TextureViewWebGpu final : public TextureView {
  public:
    TextureViewWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const TextureViewDescription& desc
    );
    ~TextureViewWebGpu() override;

    [[nodiscard]] WGPUTextureView handle() const { return m_view; }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    WGPUTextureView m_view {nullptr};
};

class ShaderModuleWebGpu final : public ShaderModule {
  public:
    ShaderModuleWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const ShaderDescription& desc
    );
    ~ShaderModuleWebGpu() override;

    [[nodiscard]] WGPUShaderModule handle() const { return m_module; }
    [[nodiscard]] WGPUStringView entry_point() const {
        return {m_entry_point.c_str(), m_entry_point.size()};
    }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    WGPUShaderModule m_module {nullptr};
    std::string m_entry_point {"main"};
};

class SamplerWebGpu final : public Sampler {
  public:
    SamplerWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const SamplerDescription& desc
    );
    ~SamplerWebGpu() override;

    [[nodiscard]] WGPUSampler handle() const { return m_sampler; }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    WGPUSampler m_sampler {nullptr};
};

class ResourceLayoutWebGpu final : public ResourceLayout {
  public:
    ResourceLayoutWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const ResourceLayoutDescription& desc
    );
    ~ResourceLayoutWebGpu() override;

    [[nodiscard]] WGPUBindGroupLayout handle() const { return m_layout; }
    [[nodiscard]] const ResourceLayoutDescription& description() const {
        return m_desc;
    }
    void adopt_pipeline_layout(WGPUBindGroupLayout layout) const;

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    ResourceLayoutDescription m_desc;
    mutable WGPUBindGroupLayout m_layout {nullptr};
};

class ResourceSetWebGpu final : public ResourceSet {
  public:
    ResourceSetWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const ResourceSetDescription& desc
    );
    ~ResourceSetWebGpu() override;

    [[nodiscard]] WGPUBindGroup handle() const { return m_group; }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    WGPUBindGroup m_group {nullptr};
    std::vector<std::shared_ptr<TextureViewWebGpu>> m_owned_views;
};

class FramebufferWebGpu final : public Framebuffer {
  public:
    using Framebuffer::Framebuffer;
};

class PipelineWebGpu final : public Pipeline {
  public:
    PipelineWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const RenderPipelineDescription& desc
    );
    PipelineWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        const ComputePipelineDescription& desc
    );
    ~PipelineWebGpu() override;

    [[nodiscard]] WGPURenderPipeline render_pipeline() const {
        return m_render_pipeline;
    }
    [[nodiscard]] WGPUComputePipeline compute_pipeline() const {
        return m_compute_pipeline;
    }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    WGPUPipelineLayout m_layout {nullptr};
    WGPURenderPipeline m_render_pipeline {nullptr};
    WGPUComputePipeline m_compute_pipeline {nullptr};
};

} // namespace fei
