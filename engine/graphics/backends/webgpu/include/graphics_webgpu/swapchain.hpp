#pragma once

#include "graphics/swapchain.hpp"
#include "graphics_webgpu/context.hpp"

#include <memory>
#include <mutex>
#include <webgpu/webgpu.h>

namespace fei {

class SwapchainWebGpu final : public Swapchain {
  public:
    SwapchainWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        WGPUSurface surface,
        uint32 width,
        uint32 height
    );
    ~SwapchainWebGpu() override;

    std::shared_ptr<const Framebuffer> framebuffer() const override;
    uint32 width() const override;
    uint32 height() const override;
    PixelFormat color_format() const override;
    void resize(uint32 width, uint32 height) override;
    void present() const override;

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    WGPUSurface m_surface {nullptr};
    mutable uint32 m_width {0};
    mutable uint32 m_height {0};
    mutable WGPUTextureFormat m_surface_format {WGPUTextureFormat_Undefined};
    mutable PixelFormat m_color_format {PixelFormat::Bgra8Unorm};
    mutable bool m_configured {false};
    mutable std::shared_ptr<const Framebuffer> m_framebuffer;
    mutable std::mutex m_mutex;

    void configure() const;
    bool acquire() const;
};

} // namespace fei
