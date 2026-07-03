#pragma once
#include "graphics/swapchain.hpp"

#include <memory>

struct GLFWwindow;

namespace fei {

class SwapchainOpenGLGlfw final : public Swapchain {
  private:
    GLFWwindow* m_window {nullptr};
    std::shared_ptr<const Framebuffer> m_framebuffer;
    uint32 m_width {0};
    uint32 m_height {0};

  public:
    SwapchainOpenGLGlfw(GLFWwindow* window, uint32 width, uint32 height);

    std::shared_ptr<const Framebuffer> framebuffer() const override;
    uint32 width() const override;
    uint32 height() const override;
    PixelFormat color_format() const override;
    void resize(uint32 width, uint32 height) override;
    void present() const override;
};

} // namespace fei
