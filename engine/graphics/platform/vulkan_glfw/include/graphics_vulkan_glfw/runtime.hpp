#pragma once

#include "graphics/backend.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace fei {

class SwapchainVulkanGlfw;

struct VulkanGlfwBootstrapDescription {
    std::vector<std::string> required_instance_extensions;
    std::vector<std::string> required_device_extensions;
    GLFWwindow* window {nullptr};
    GraphicsSurfaceSize surface_size;
};

class VulkanGlfwRuntime final : public GraphicsRuntime {
  public:
    explicit VulkanGlfwRuntime(VulkanGlfwBootstrapDescription description);
    ~VulkanGlfwRuntime() override;

    [[nodiscard]] const GraphicsBackendCapabilities&
    capabilities() const noexcept override;
    GraphicsDevice& device() noexcept override;
    const GraphicsDevice& device() const noexcept override;
    std::shared_ptr<Swapchain> presentation_target() noexcept override;
    std::shared_ptr<const Swapchain>
    presentation_target() const noexcept override;

    void resize(std::uint32_t width, std::uint32_t height) override;
    void flush() const override;
    void present() const override;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

class VulkanGlfwBootstrap final : public GraphicsBackendBootstrap {
  public:
    explicit VulkanGlfwBootstrap(VulkanGlfwBootstrapDescription description);

    [[nodiscard]] const GraphicsBackendCapabilities&
    capabilities() const noexcept override;
    std::unique_ptr<GraphicsRuntime> initialize() override;

  private:
    std::optional<VulkanGlfwBootstrapDescription> m_description;
};

} // namespace fei
