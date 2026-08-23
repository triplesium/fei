#pragma once

#include "graphics/texture.hpp"

#include <memory>
#include <vector>
#include <webgpu/webgpu.h>

namespace ets {

class TextureWebGpu;
class WebGpuDeviceState;

[[nodiscard]] bool
supports_webgpu_mipmap_generation(const TextureDescription& desc);

class MipmapGeneratorWebGpu {
  public:
    explicit MipmapGeneratorWebGpu(const WebGpuDeviceState& state);
    ~MipmapGeneratorWebGpu();

    MipmapGeneratorWebGpu(const MipmapGeneratorWebGpu&) = delete;
    MipmapGeneratorWebGpu& operator=(const MipmapGeneratorWebGpu&) = delete;
    MipmapGeneratorWebGpu(MipmapGeneratorWebGpu&&) = delete;
    MipmapGeneratorWebGpu& operator=(MipmapGeneratorWebGpu&&) = delete;

    void encode(
        WGPUCommandEncoder encoder,
        const TextureWebGpu& texture,
        std::vector<std::shared_ptr<const void>>& retained_resources
    ) const;

  private:
    WGPUDevice m_device {nullptr};
    WGPUShaderModule m_shader {nullptr};
    WGPUBindGroupLayout m_bind_group_layout {nullptr};
    WGPUPipelineLayout m_pipeline_layout {nullptr};
    WGPUComputePipeline m_pipeline {nullptr};
};

} // namespace ets
