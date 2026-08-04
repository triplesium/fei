#pragma once

#include "ecs/resource_traits.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"

#include <cstdint>
#include <memory>

namespace fei {

enum class GraphicsBackendKind : std::uint8_t {
    Unknown,
    OpenGL,
    Vulkan,
    WebGpu,
};

struct GraphicsBackendCapabilities {
    GraphicsBackendKind backend {GraphicsBackendKind::Unknown};
    bool surface_creation_on_main_thread {true};
    bool presentation_on_render_thread {true};
    bool context_transfer_required {false};
    bool explicit_device_polling {false};

    bool operator==(const GraphicsBackendCapabilities&) const = default;
};

struct GraphicsSurfaceSize {
    std::uint32_t width {0};
    std::uint32_t height {0};

    bool operator==(const GraphicsSurfaceSize&) const = default;
};

// Owns or borrows the backend objects used by one Render App. Implementations
// define the thread on which initialize, frame execution, and destruction run.
class GraphicsRuntime {
  public:
    virtual ~GraphicsRuntime() = default;

    [[nodiscard]] virtual const GraphicsBackendCapabilities&
    capabilities() const noexcept = 0;
    virtual GraphicsDevice& device() noexcept = 0;
    virtual const GraphicsDevice& device() const noexcept = 0;
    virtual std::shared_ptr<Swapchain> presentation_target() noexcept = 0;
    virtual std::shared_ptr<const Swapchain>
    presentation_target() const noexcept = 0;

    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;
    virtual void flush() const = 0;
    virtual void present() const = 0;
};

// Created after any platform-main-thread preparation. initialize() is called
// on the thread that will own the resulting GraphicsRuntime.
class GraphicsBackendBootstrap {
  public:
    virtual ~GraphicsBackendBootstrap() = default;

    [[nodiscard]] virtual const GraphicsBackendCapabilities&
    capabilities() const noexcept = 0;
    virtual std::unique_ptr<GraphicsRuntime> initialize() = 0;
};

class BoxedGraphicsBackendBootstrap final : public GraphicsBackendBootstrap {
  public:
    explicit BoxedGraphicsBackendBootstrap(
        std::unique_ptr<GraphicsBackendBootstrap> bootstrap
    );

    [[nodiscard]] const GraphicsBackendCapabilities&
    capabilities() const noexcept override {
        return m_bootstrap->capabilities();
    }
    std::unique_ptr<GraphicsRuntime> initialize() override {
        return m_bootstrap->initialize();
    }

  private:
    std::unique_ptr<GraphicsBackendBootstrap> m_bootstrap;
};

// Adapts a dynamically created backend runtime into the ECS resource model,
// which stores concrete values behind the GraphicsRuntime base type.
class BoxedGraphicsRuntime final : public GraphicsRuntime {
  public:
    explicit BoxedGraphicsRuntime(std::unique_ptr<GraphicsRuntime> runtime);

    [[nodiscard]] const GraphicsBackendCapabilities&
    capabilities() const noexcept override {
        return m_runtime->capabilities();
    }

    GraphicsDevice& device() noexcept override { return m_runtime->device(); }
    const GraphicsDevice& device() const noexcept override {
        return m_runtime->device();
    }
    std::shared_ptr<Swapchain> presentation_target() noexcept override {
        return m_runtime->presentation_target();
    }
    std::shared_ptr<const Swapchain>
    presentation_target() const noexcept override {
        return m_runtime->presentation_target();
    }

    void resize(std::uint32_t width, std::uint32_t height) override {
        m_runtime->resize(width, height);
    }
    void flush() const override { m_runtime->flush(); }
    void present() const override { m_runtime->present(); }

  private:
    std::unique_ptr<GraphicsRuntime> m_runtime;
};

template<>
struct ResourceTraits<GraphicsRuntime> {
    static constexpr bool main_thread_only = false;
};

template<>
struct ResourceTraits<GraphicsBackendBootstrap> {
    static constexpr bool main_thread_only = false;
};

} // namespace fei
