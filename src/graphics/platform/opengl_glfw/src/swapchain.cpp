#include "graphics_opengl_glfw/swapchain.hpp"

#include "base/log.hpp"
#include "graphics_opengl/framebuffer.hpp"
#include "profiling/profiling.hpp"

#include <GLFW/glfw3.h>

namespace fei {

SwapchainOpenGLGlfw::SwapchainOpenGLGlfw(
    GLFWwindow* window,
    uint32 width,
    uint32 height
) :
    m_window(window),
    m_framebuffer(
        FramebufferOpenGL::default_framebuffer(PixelFormat::Bgra8Unorm)
    ),
    m_width(width), m_height(height) {
    if (m_window == nullptr) {
        fatal("SwapchainOpenGLGlfw requires a valid GLFW window");
    }
    if (!m_framebuffer) {
        fatal("SwapchainOpenGLGlfw requires a valid framebuffer");
    }
}

std::shared_ptr<const Framebuffer> SwapchainOpenGLGlfw::framebuffer() const {
    return m_framebuffer;
}

uint32 SwapchainOpenGLGlfw::width() const {
    return m_width;
}

uint32 SwapchainOpenGLGlfw::height() const {
    return m_height;
}

PixelFormat SwapchainOpenGLGlfw::color_format() const {
    return PixelFormat::Bgra8Unorm;
}

void SwapchainOpenGLGlfw::resize(uint32 width, uint32 height) {
    m_width = width;
    m_height = height;
}

void SwapchainOpenGLGlfw::present() const {
    FEI_PROFILE_SCOPE("OpenGL GLFW Swap Buffers");
    glfwSwapBuffers(m_window);
}

} // namespace fei
