#pragma once

#include "graphics/backend.hpp"

#include <memory>
#include <optional>

struct GLFWwindow;

namespace fei {

struct OpenGLGlfwBootstrapDescription {
    GLFWwindow* window {nullptr};
    GraphicsSurfaceSize surface_size;
};

class OpenGLGlfwRuntime final : public GraphicsRuntime {
  public:
    explicit OpenGLGlfwRuntime(OpenGLGlfwBootstrapDescription description);
    ~OpenGLGlfwRuntime() override;

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

class OpenGLGlfwBootstrap final : public GraphicsBackendBootstrap {
  public:
    explicit OpenGLGlfwBootstrap(OpenGLGlfwBootstrapDescription description);

    [[nodiscard]] const GraphicsBackendCapabilities&
    capabilities() const noexcept override;
    std::unique_ptr<GraphicsRuntime> initialize() override;

  private:
    std::optional<OpenGLGlfwBootstrapDescription> m_description;
};

} // namespace fei
