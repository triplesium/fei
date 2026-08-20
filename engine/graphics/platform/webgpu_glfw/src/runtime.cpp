#include "graphics_webgpu_glfw/runtime.hpp"

#include "graphics_webgpu/context.hpp"
#include "graphics_webgpu/graphics_device.hpp"
#include "graphics_webgpu/swapchain.hpp"

#ifndef GLFW_INCLUDE_NONE
#    define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <stdexcept>

namespace fei {

namespace {

const GraphicsBackendCapabilities& webgpu_glfw_capabilities() {
    static const GraphicsBackendCapabilities capabilities {
        .backend = GraphicsBackendKind::WebGpu,
        .surface_creation_on_main_thread = false,
        .presentation_on_render_thread = true,
        .explicit_device_polling = true,
    };
    return capabilities;
}

WGPUSurface create_surface(WGPUInstance instance, GLFWwindow* window) {
    if (window == nullptr) {
        wgpuInstanceRelease(instance);
        throw std::invalid_argument("WebGpuGlfwRuntime requires a GLFW window");
    }
    auto surface = glfwCreateWindowWGPUSurface(instance, window);
    if (surface == nullptr) {
        wgpuInstanceRelease(instance);
        throw std::runtime_error("Failed to create WebGPU surface");
    }
    return surface;
}

} // namespace

class WebGpuGlfwRuntime::Impl {
  public:
    explicit Impl(WebGpuGlfwBootstrapDescription description) :
        instance(create_webgpu_instance()),
        surface(create_surface(instance, description.window)),
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

WebGpuGlfwRuntime::WebGpuGlfwRuntime(
    WebGpuGlfwBootstrapDescription description
) : m_impl(std::make_unique<Impl>(description)) {}

WebGpuGlfwRuntime::~WebGpuGlfwRuntime() = default;

const GraphicsBackendCapabilities&
WebGpuGlfwRuntime::capabilities() const noexcept {
    return webgpu_glfw_capabilities();
}

GraphicsDevice& WebGpuGlfwRuntime::device() noexcept {
    return m_impl->device;
}

const GraphicsDevice& WebGpuGlfwRuntime::device() const noexcept {
    return m_impl->device;
}

std::shared_ptr<Swapchain> WebGpuGlfwRuntime::presentation_target() noexcept {
    return m_impl->swapchain;
}

std::shared_ptr<const Swapchain>
WebGpuGlfwRuntime::presentation_target() const noexcept {
    return m_impl->swapchain;
}

void WebGpuGlfwRuntime::resize(std::uint32_t width, std::uint32_t height) {
    m_impl->swapchain->resize(width, height);
}

void WebGpuGlfwRuntime::flush() const {
    m_impl->device.flush();
}

void WebGpuGlfwRuntime::present() const {
    m_impl->device.present(*m_impl->swapchain);
}

WebGpuGlfwBootstrap::WebGpuGlfwBootstrap(
    WebGpuGlfwBootstrapDescription description
) : m_description(description) {}

const GraphicsBackendCapabilities&
WebGpuGlfwBootstrap::capabilities() const noexcept {
    return webgpu_glfw_capabilities();
}

std::unique_ptr<GraphicsRuntime> WebGpuGlfwBootstrap::initialize() {
    if (!m_description) {
        throw std::logic_error("WebGpuGlfwBootstrap is already initialized");
    }
    auto description = *m_description;
    m_description.reset();
    return std::make_unique<WebGpuGlfwRuntime>(description);
}

} // namespace fei
