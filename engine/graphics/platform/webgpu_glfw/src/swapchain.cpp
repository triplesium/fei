#include "graphics_webgpu_glfw/swapchain.hpp"

#include "base/log.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics_webgpu/resources.hpp"
#include "graphics_webgpu/utils.hpp"

#include <array>
#include <utility>

namespace fei {

namespace {

WGPUTextureFormat
choose_surface_format(const WGPUSurfaceCapabilities& capabilities) {
    constexpr std::array preferred {
        WGPUTextureFormat_BGRA8Unorm,
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureFormat_BGRA8UnormSrgb,
        WGPUTextureFormat_RGBA8UnormSrgb,
    };
    for (const auto wanted : preferred) {
        for (std::size_t index = 0; index < capabilities.formatCount; ++index) {
            if (capabilities.formats[index] == wanted) {
                return wanted;
            }
        }
    }
    fatal("WebGPU surface reports no supported BGRA/RGBA format");
}

TextureDescription
swapchain_texture_description(uint32 width, uint32 height, PixelFormat format) {
    return TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = format,
        .texture_usage = TextureUsage::RenderTarget,
        .texture_type = TextureType::Texture2D,
        .sample_count = TextureSampleCount::Count1,
    };
}

} // namespace

SwapchainWebGpuGlfw::SwapchainWebGpuGlfw(
    std::shared_ptr<WebGpuDeviceState> state,
    WGPUSurface surface,
    uint32 width,
    uint32 height
) :
    m_state(std::move(state)), m_surface(surface), m_width(width),
    m_height(height) {
    if (!m_state || m_surface == nullptr) {
        fatal("SwapchainWebGpuGlfw requires a device state and surface");
    }
    configure();
}

SwapchainWebGpuGlfw::~SwapchainWebGpuGlfw() {
    std::scoped_lock lock(m_mutex);
    m_framebuffer.reset();
    if (m_configured) {
        wgpuSurfaceUnconfigure(m_surface);
    }
    if (m_surface != nullptr) {
        wgpuSurfaceRelease(m_surface);
    }
}

void SwapchainWebGpuGlfw::configure() const {
    m_framebuffer.reset();
    if (m_width == 0 || m_height == 0) {
        if (m_configured) {
            wgpuSurfaceUnconfigure(m_surface);
            m_configured = false;
        }
        return;
    }

    WGPUSurfaceCapabilities capabilities {};
    if (wgpuSurfaceGetCapabilities(
            m_surface,
            m_state->adapter(),
            &capabilities
        ) != WGPUStatus_Success) {
        fatal("Failed to query WebGPU surface capabilities");
    }
    m_surface_format = choose_surface_format(capabilities);
    m_color_format = from_webgpu(m_surface_format);

    WGPUSurfaceConfiguration configuration {};
    configuration.device = m_state->device();
    configuration.format = m_surface_format;
    configuration.usage =
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    configuration.width = m_width;
    configuration.height = m_height;
    configuration.alphaMode = capabilities.alphaModeCount != 0 ?
                                  capabilities.alphaModes[0] :
                                  WGPUCompositeAlphaMode_Auto;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(m_surface, &configuration);
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    m_configured = true;
}

bool SwapchainWebGpuGlfw::acquire() const {
    if (m_framebuffer) {
        return true;
    }
    if (!m_configured) {
        configure();
    }
    if (!m_configured) {
        return false;
    }

    WGPUSurfaceTexture surface_texture {
        .status = WGPUSurfaceGetCurrentTextureStatus_Force32,
    };
    wgpuSurfaceGetCurrentTexture(m_surface, &surface_texture);
    if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
        surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
        if (surface_texture.texture != nullptr) {
            wgpuTextureRelease(surface_texture.texture);
        }
        configure();
        wgpuSurfaceGetCurrentTexture(m_surface, &surface_texture);
    }
    if (surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        if (surface_texture.texture != nullptr) {
            wgpuTextureRelease(surface_texture.texture);
        }
        if (surface_texture.status ==
            WGPUSurfaceGetCurrentTextureStatus_Timeout) {
            return false;
        }
        fatal(
            "Failed to acquire WebGPU surface texture (status {})",
            static_cast<int>(surface_texture.status)
        );
    }

    auto texture = std::make_shared<TextureWebGpu>(
        m_state,
        swapchain_texture_description(m_width, m_height, m_color_format),
        surface_texture.texture
    );
    m_framebuffer = std::make_shared<FramebufferWebGpu>(FramebufferDescription {
        .color_targets = {
            FramebufferAttachment {
                .texture = std::move(texture),
                .mip_level = 0,
                .layer = 0,
            },
        },
    });
    return true;
}

std::shared_ptr<const Framebuffer> SwapchainWebGpuGlfw::framebuffer() const {
    std::scoped_lock lock(m_mutex);
    acquire();
    return m_framebuffer;
}

uint32 SwapchainWebGpuGlfw::width() const {
    std::scoped_lock lock(m_mutex);
    return m_width;
}

uint32 SwapchainWebGpuGlfw::height() const {
    std::scoped_lock lock(m_mutex);
    return m_height;
}

PixelFormat SwapchainWebGpuGlfw::color_format() const {
    std::scoped_lock lock(m_mutex);
    return m_color_format;
}

void SwapchainWebGpuGlfw::resize(uint32 width, uint32 height) {
    std::scoped_lock lock(m_mutex);
    if (m_width == width && m_height == height) {
        return;
    }
    m_width = width;
    m_height = height;
    configure();
}

void SwapchainWebGpuGlfw::present() const {
    std::scoped_lock lock(m_mutex);
    if (!m_framebuffer) {
        return;
    }
    m_framebuffer.reset();
    const auto status = wgpuSurfacePresent(m_surface);
    if (status != WGPUStatus_Success) {
        m_configured = false;
        error("Failed to present WebGPU surface");
    }
}

} // namespace fei
