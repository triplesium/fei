#include "graphics_vulkan_glfw/runtime.hpp"

#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/graphics_device.hpp"
#include "graphics_vulkan_glfw/swapchain.hpp"

#include <stdexcept>
#include <utility>

namespace ets {

namespace {

const GraphicsBackendCapabilities& vulkan_glfw_capabilities() {
    static const GraphicsBackendCapabilities capabilities {
        .backend = GraphicsBackendKind::Vulkan,
        .surface_creation_on_main_thread = false,
        .presentation_on_render_thread = true,
    };
    return capabilities;
}

} // namespace

class VulkanGlfwRuntime::Impl {
  public:
    explicit Impl(VulkanGlfwBootstrapDescription description) :
        device(
            VulkanDeviceStateDescription {
                .required_instance_extensions =
                    std::move(description.required_instance_extensions),
                .required_device_extensions =
                    std::move(description.required_device_extensions),
            }
        ),
        swapchain(
            std::make_shared<SwapchainVulkanGlfw>(
                device.state(),
                description.window,
                description.surface_size.width,
                description.surface_size.height
            )
        ) {}

    GraphicsDeviceVulkan device;
    std::shared_ptr<SwapchainVulkanGlfw> swapchain;
};

VulkanGlfwRuntime::VulkanGlfwRuntime(
    VulkanGlfwBootstrapDescription description
) : m_impl(std::make_unique<Impl>(std::move(description))) {}

VulkanGlfwRuntime::~VulkanGlfwRuntime() = default;

const GraphicsBackendCapabilities&
VulkanGlfwRuntime::capabilities() const noexcept {
    return vulkan_glfw_capabilities();
}

GraphicsDevice& VulkanGlfwRuntime::device() noexcept {
    return m_impl->device;
}

const GraphicsDevice& VulkanGlfwRuntime::device() const noexcept {
    return m_impl->device;
}

std::shared_ptr<Swapchain> VulkanGlfwRuntime::presentation_target() noexcept {
    return m_impl->swapchain;
}

std::shared_ptr<const Swapchain>
VulkanGlfwRuntime::presentation_target() const noexcept {
    return m_impl->swapchain;
}

void VulkanGlfwRuntime::resize(std::uint32_t width, std::uint32_t height) {
    m_impl->swapchain->resize(width, height);
}

void VulkanGlfwRuntime::flush() const {
    m_impl->device.flush();
}

void VulkanGlfwRuntime::present() const {
    m_impl->device.present(*m_impl->swapchain);
}

VulkanGlfwBootstrap::VulkanGlfwBootstrap(
    VulkanGlfwBootstrapDescription description
) : m_description(std::move(description)) {}

const GraphicsBackendCapabilities&
VulkanGlfwBootstrap::capabilities() const noexcept {
    return vulkan_glfw_capabilities();
}

std::unique_ptr<GraphicsRuntime> VulkanGlfwBootstrap::initialize() {
    if (!m_description) {
        throw std::logic_error("VulkanGlfwBootstrap is already initialized");
    }
    auto description = std::move(*m_description);
    m_description.reset();
    return std::make_unique<VulkanGlfwRuntime>(std::move(description));
}

} // namespace ets
