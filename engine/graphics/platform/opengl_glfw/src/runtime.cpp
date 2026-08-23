#include "graphics_opengl_glfw/runtime.hpp"

#include "graphics_opengl/graphics_device.hpp"
#include "graphics_opengl_glfw/swapchain.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace ets {

namespace {

const GraphicsBackendCapabilities& opengl_glfw_capabilities() {
    static const GraphicsBackendCapabilities capabilities {
        .backend = GraphicsBackendKind::OpenGL,
        .surface_creation_on_main_thread = true,
        .presentation_on_render_thread = true,
        .context_transfer_required = true,
    };
    return capabilities;
}

class GlfwContextBinding {
  public:
    explicit GlfwContextBinding(GLFWwindow* window) : m_window(window) {
        if (m_window == nullptr) {
            throw std::invalid_argument(
                "OpenGLGlfwRuntime requires a GLFW window"
            );
        }
        if (glfwGetCurrentContext() != nullptr) {
            throw std::runtime_error(
                "OpenGL Render Worker already owns a GLFW context"
            );
        }

        glfwMakeContextCurrent(m_window);
        if (glfwGetCurrentContext() != m_window) {
            throw std::runtime_error(
                "Failed to make the OpenGL context current on the Render "
                "Worker"
            );
        }
        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(glfwGetProcAddress)
            )) {
            glfwMakeContextCurrent(nullptr);
            throw std::runtime_error("Failed to initialize GLAD");
        }
    }

    ~GlfwContextBinding() { glfwMakeContextCurrent(nullptr); }

    GlfwContextBinding(const GlfwContextBinding&) = delete;
    GlfwContextBinding& operator=(const GlfwContextBinding&) = delete;

  private:
    GLFWwindow* m_window;
};

} // namespace

class OpenGLGlfwRuntime::Impl {
  public:
    explicit Impl(OpenGLGlfwBootstrapDescription description) :
        context(description.window),
        device(std::make_unique<GraphicsDeviceOpenGL>()),
        swapchain(
            std::make_shared<SwapchainOpenGLGlfw>(
                description.window,
                description.surface_size.width,
                description.surface_size.height
            )
        ) {}

    GlfwContextBinding context;
    std::unique_ptr<GraphicsDeviceOpenGL> device;
    std::shared_ptr<SwapchainOpenGLGlfw> swapchain;
};

OpenGLGlfwRuntime::OpenGLGlfwRuntime(
    OpenGLGlfwBootstrapDescription description
) : m_impl(std::make_unique<Impl>(description)) {}

OpenGLGlfwRuntime::~OpenGLGlfwRuntime() = default;

const GraphicsBackendCapabilities&
OpenGLGlfwRuntime::capabilities() const noexcept {
    return opengl_glfw_capabilities();
}

GraphicsDevice& OpenGLGlfwRuntime::device() noexcept {
    return *m_impl->device;
}

const GraphicsDevice& OpenGLGlfwRuntime::device() const noexcept {
    return *m_impl->device;
}

std::shared_ptr<Swapchain> OpenGLGlfwRuntime::presentation_target() noexcept {
    return m_impl->swapchain;
}

std::shared_ptr<const Swapchain>
OpenGLGlfwRuntime::presentation_target() const noexcept {
    return m_impl->swapchain;
}

void OpenGLGlfwRuntime::resize(std::uint32_t width, std::uint32_t height) {
    m_impl->swapchain->resize(width, height);
}

void OpenGLGlfwRuntime::flush() const {
    m_impl->device->flush();
}

void OpenGLGlfwRuntime::present() const {
    m_impl->device->present(*m_impl->swapchain);
}

OpenGLGlfwBootstrap::OpenGLGlfwBootstrap(
    OpenGLGlfwBootstrapDescription description
) : m_description(description) {}

const GraphicsBackendCapabilities&
OpenGLGlfwBootstrap::capabilities() const noexcept {
    return opengl_glfw_capabilities();
}

std::unique_ptr<GraphicsRuntime> OpenGLGlfwBootstrap::initialize() {
    if (!m_description) {
        throw std::logic_error("OpenGLGlfwBootstrap is already initialized");
    }
    auto description = *m_description;
    m_description.reset();
    return std::make_unique<OpenGLGlfwRuntime>(description);
}

} // namespace ets
