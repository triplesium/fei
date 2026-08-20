#include "graphics_webgpu_browser/runtime.hpp"

#include "graphics_webgpu/context.hpp"
#include "graphics_webgpu/graphics_device.hpp"
#include "graphics_webgpu/swapchain.hpp"

#include <stdexcept>
#include <utility>
#include <webgpu/webgpu.h>

namespace fei {

namespace {

const GraphicsBackendCapabilities& webgpu_browser_capabilities() {
    static const GraphicsBackendCapabilities capabilities {
        .backend = GraphicsBackendKind::WebGpu,
        .surface_creation_on_main_thread = true,
        .presentation_on_render_thread = false,
        .explicit_device_polling = false,
    };
    return capabilities;
}

WGPUSurface create_surface(WGPUInstance instance, const std::string& selector) {
    if (selector.empty()) {
        wgpuInstanceRelease(instance);
        throw std::invalid_argument(
            "WebGpuBrowserRuntime requires a canvas selector"
        );
    }

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source {
        .chain =
            {
                .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector,
            },
        .selector = {selector.data(), selector.size()},
    };
    WGPUSurfaceDescriptor descriptor {
        .nextInChain = &canvas_source.chain,
        .label = {"fei browser canvas", WGPU_STRLEN},
    };
    auto surface = wgpuInstanceCreateSurface(instance, &descriptor);
    if (surface == nullptr) {
        wgpuInstanceRelease(instance);
        throw std::runtime_error("Failed to create browser WebGPU surface");
    }
    return surface;
}

} // namespace

class WebGpuBrowserRuntime::Impl {
  public:
    explicit Impl(WebGpuBrowserBootstrapDescription description) :
        instance(create_webgpu_instance()),
        surface(create_surface(instance, description.canvas_selector)),
        device(
            WebGpuDeviceStateDescription {
                .instance = instance,
                .compatible_surface = surface,
            }
        ),
        swapchain(
            std::make_shared<SwapchainWebGpu>(
                device.state(),
                surface,
                description.surface_size.width,
                description.surface_size.height
            )
        ) {}

    WGPUInstance instance {nullptr};
    WGPUSurface surface {nullptr};
    GraphicsDeviceWebGpu device;
    std::shared_ptr<SwapchainWebGpu> swapchain;
};

WebGpuBrowserRuntime::WebGpuBrowserRuntime(
    WebGpuBrowserBootstrapDescription description
) : m_impl(std::make_unique<Impl>(std::move(description))) {}

WebGpuBrowserRuntime::~WebGpuBrowserRuntime() = default;

const GraphicsBackendCapabilities&
WebGpuBrowserRuntime::capabilities() const noexcept {
    return webgpu_browser_capabilities();
}

GraphicsDevice& WebGpuBrowserRuntime::device() noexcept {
    return m_impl->device;
}

const GraphicsDevice& WebGpuBrowserRuntime::device() const noexcept {
    return m_impl->device;
}

std::shared_ptr<Swapchain>
WebGpuBrowserRuntime::presentation_target() noexcept {
    return m_impl->swapchain;
}

std::shared_ptr<const Swapchain>
WebGpuBrowserRuntime::presentation_target() const noexcept {
    return m_impl->swapchain;
}

void WebGpuBrowserRuntime::resize(std::uint32_t width, std::uint32_t height) {
    m_impl->swapchain->resize(width, height);
}

void WebGpuBrowserRuntime::flush() const {
    m_impl->device.flush();
}

void WebGpuBrowserRuntime::present() const {
    m_impl->device.present(*m_impl->swapchain);
}

WebGpuBrowserBootstrap::WebGpuBrowserBootstrap(
    WebGpuBrowserBootstrapDescription description
) : m_description(std::move(description)) {}

const GraphicsBackendCapabilities&
WebGpuBrowserBootstrap::capabilities() const noexcept {
    return webgpu_browser_capabilities();
}

std::unique_ptr<GraphicsRuntime> WebGpuBrowserBootstrap::initialize() {
    if (!m_description) {
        throw std::logic_error("WebGpuBrowserBootstrap is already initialized");
    }
    auto description = std::move(*m_description);
    m_description.reset();
    return std::make_unique<WebGpuBrowserRuntime>(std::move(description));
}

} // namespace fei
