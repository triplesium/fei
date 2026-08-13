#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "core/transform_plugin.hpp"
#include "graphics/backend.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_app.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/resource_set_cache.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/view.hpp"
#include "rendering/visibility.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class FakeSwapchain final : public Swapchain {
  public:
    std::shared_ptr<const Framebuffer> framebuffer() const override {
        return nullptr;
    }
    uint32 width() const override { return 1; }
    uint32 height() const override { return 1; }
    PixelFormat color_format() const override {
        return PixelFormat::Rgba8Unorm;
    }
    void resize(uint32, uint32) override {}
    void present() const override {}
};

struct BootstrapThreadState {
    std::thread::id initialized_on;
    std::thread::id presented_on;
    std::thread::id destroyed_on;
    GraphicsSurfaceSize resized_to;
};

GraphicsBackendCapabilities vulkan_capabilities() {
    return GraphicsBackendCapabilities {
        .backend = GraphicsBackendKind::Vulkan,
    };
}

class FakeOwnedGraphicsRuntime final : public GraphicsRuntime {
  public:
    explicit FakeOwnedGraphicsRuntime(
        std::shared_ptr<BootstrapThreadState> state,
        GraphicsBackendCapabilities capabilities = vulkan_capabilities()
    ) :
        m_capabilities(capabilities), m_state(std::move(state)),
        m_swapchain(std::make_shared<FakeSwapchain>()) {
        m_state->initialized_on = std::this_thread::get_id();
    }

    ~FakeOwnedGraphicsRuntime() override {
        m_state->destroyed_on = std::this_thread::get_id();
    }

    const GraphicsBackendCapabilities& capabilities() const noexcept override {
        return m_capabilities;
    }
    GraphicsDevice& device() noexcept override { return m_device; }
    const GraphicsDevice& device() const noexcept override { return m_device; }
    std::shared_ptr<Swapchain> presentation_target() noexcept override {
        return m_swapchain;
    }
    std::shared_ptr<const Swapchain>
    presentation_target() const noexcept override {
        return m_swapchain;
    }
    void resize(std::uint32_t width, std::uint32_t height) override {
        m_state->resized_to = GraphicsSurfaceSize {width, height};
        m_swapchain->resize(width, height);
    }
    void flush() const override { m_device.flush(); }
    void present() const override {
        m_state->presented_on = std::this_thread::get_id();
        m_device.present(*m_swapchain);
    }

  private:
    GraphicsBackendCapabilities m_capabilities;
    std::shared_ptr<BootstrapThreadState> m_state;
    FakeGraphicsDevice m_device;
    std::shared_ptr<FakeSwapchain> m_swapchain;
};

class FakeGraphicsBootstrap final : public GraphicsBackendBootstrap {
  public:
    explicit FakeGraphicsBootstrap(
        std::shared_ptr<BootstrapThreadState> state,
        GraphicsBackendCapabilities runtime_capabilities = vulkan_capabilities()
    ) :
        m_state(std::move(state)),
        m_runtime_capabilities(runtime_capabilities) {}

    const GraphicsBackendCapabilities& capabilities() const noexcept override {
        return m_capabilities;
    }
    std::unique_ptr<GraphicsRuntime> initialize() override {
        return std::make_unique<FakeOwnedGraphicsRuntime>(
            m_state,
            m_runtime_capabilities
        );
    }

  private:
    GraphicsBackendCapabilities m_capabilities {vulkan_capabilities()};
    std::shared_ptr<BootstrapThreadState> m_state;
    GraphicsBackendCapabilities m_runtime_capabilities;
};

} // namespace

TEST_CASE(
    "RenderingPlugin skips present when MainSwapchain is absent",
    "[rendering][plugin]"
) {
    App app;
    app.add_plugin<AssetsPlugin>();
    install_render_app(app);
    app.sub_app<RenderApp>().add_resource_as<GraphicsDevice>(
        FakeGraphicsDevice {}
    );
    app.add_plugin<RenderingPlugin>();
    app.finish();

    REQUIRE(app.has_plugin<TransformPlugin>());

    auto& render_world = app.sub_app<RenderApp>().world();
    REQUIRE(render_world.has_local_resource<GraphicsDevice>());
    REQUIRE(render_world.has_local_resource<SlangLibraryShaderCompiler>());
    REQUIRE(render_world.has_local_resource<ShaderVariantCompiler>());
    REQUIRE(render_world.has_local_resource<PipelineCache>());
    REQUIRE(render_world.has_local_resource<RenderFrameContext>());
    REQUIRE(render_world.has_local_resource<RenderQueue>());
    REQUIRE(render_world.has_local_resource<RenderResourceSetCache>());
    REQUIRE(render_world.has_local_resource<ViewUniforms>());
    REQUIRE(render_world.has_local_resource<ShaderCache>());
    REQUIRE(render_world.has_local_resource<MeshUniforms>());
    REQUIRE(render_world.has_local_resource<ViewVisibleEntities>());
    REQUIRE(render_world.has_local_resource<RenderingDefaults>());
    REQUIRE(render_world.has_local_resource<RenderAssets<GpuImage>>());
    REQUIRE(render_world.has_local_resource<RenderAssets<GpuMesh>>());

    REQUIRE_FALSE(app.world().has_local_resource<PipelineCache>());
    REQUIRE_FALSE(app.world().has_local_resource<SlangLibraryShaderCompiler>());
    REQUIRE_FALSE(app.world().has_local_resource<ShaderVariantCompiler>());
    REQUIRE_FALSE(app.world().has_local_resource<RenderFrameContext>());
    REQUIRE_FALSE(app.world().has_local_resource<RenderQueue>());
    REQUIRE_FALSE(app.world().has_local_resource<RenderResourceSetCache>());
    REQUIRE_FALSE(app.world().has_local_resource<ViewUniforms>());
    REQUIRE_FALSE(app.world().has_local_resource<ShaderCache>());
    REQUIRE_FALSE(app.world().has_local_resource<MeshUniforms>());
    REQUIRE_FALSE(app.world().has_local_resource<ViewVisibleEntities>());
    REQUIRE_FALSE(app.world().has_local_resource<RenderingDefaults>());
    REQUIRE_FALSE(app.world().has_local_resource<RenderAssets<GpuImage>>());
    REQUIRE_FALSE(app.world().has_local_resource<RenderAssets<GpuMesh>>());

    REQUIRE_FALSE(app.has_resource<GraphicsDevice>());
    auto& device = dynamic_cast<FakeGraphicsDevice&>(
        render_world.resource<GraphicsDevice>()
    );
    REQUIRE(
        &static_cast<const World&>(render_world).resource<GraphicsDevice>() ==
        &device
    );

    auto shader = app.resource<Assets<Shader>>().add(
        std::make_unique<Shader>(Shader {
            .path = "extracted.slang",
            .source = R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)",
        })
    );

    app.startup();
    REQUIRE(render_world.resource<RenderingDefaults>().default_texture);
    auto first_shader_module = render_world.resource<ShaderCache>().get(
        shader,
        ShaderStages::Fragment,
        "fragment_main"
    );

    auto modified_shader = app.resource<Assets<Shader>>().modify(shader);
    REQUIRE(modified_shader);
    modified_shader->source = R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
)";
    app.update();
    app.render();

    auto second_shader_module = render_world.resource<ShaderCache>().get(
        shader,
        ShaderStages::Fragment,
        "fragment_main"
    );
    REQUIRE(second_shader_module != first_shader_module);

    app.sub_app<RenderApp>().run_schedule(RenderLast);

    REQUIRE(device.present_calls == 0);
}

TEST_CASE(
    "RenderingPlugin rejects Main World graphics resources",
    "[rendering][plugin][graphics-runtime][main-world]"
) {
    App app;
    app.add_plugin<AssetsPlugin>().add_resource_as<GraphicsDevice>(
        FakeGraphicsDevice {}
    );

    app.add_plugin<RenderingPlugin>();
    REQUIRE_THROWS_AS(app.finish(), std::runtime_error);
}

TEST_CASE(
    "RenderingPlugin selects a worker for a graphics bootstrap",
    "[rendering][plugin][graphics-runtime][bootstrap][threaded]"
) {
    const auto caller_thread = std::this_thread::get_id();
    auto state = std::make_shared<BootstrapThreadState>();

    App app;
    app.add_plugin<AssetsPlugin>()
        .add_resource_as<GraphicsBackendBootstrap>(
            BoxedGraphicsBackendBootstrap(
                std::make_unique<FakeGraphicsBootstrap>(state)
            )
        )
        .add_resource(GraphicsSurfaceSize {.width = 640, .height = 360});
    app.add_plugin<RenderingPlugin>();
    app.finish();

    REQUIRE(
        app.sub_app_runner<RenderApp>().execution_mode() ==
        SubAppExecutionMode::DedicatedThread
    );

    auto& render_world = app.sub_app<RenderApp>().world();
    REQUIRE(render_world.has_local_resource<GraphicsRuntime>());
    REQUIRE(render_world.has_local_resource<GraphicsDevice>());
    REQUIRE(render_world.has_local_resource<MainSwapchain>());
    REQUIRE_FALSE(app.has_resource<GraphicsRuntime>());
    REQUIRE_FALSE(app.has_resource<GraphicsDevice>());
    REQUIRE_FALSE(app.has_resource<MainSwapchain>());
    REQUIRE(state->initialized_on != caller_thread);

    app.render();
    app.shutdown();

    REQUIRE(state->resized_to == GraphicsSurfaceSize {640, 360});
    REQUIRE(state->presented_on == state->initialized_on);
    REQUIRE(state->destroyed_on == state->initialized_on);
}

TEST_CASE(
    "RenderingPlugin rejects an inline runner for a graphics bootstrap",
    "[rendering][plugin][graphics-runtime][bootstrap][inline]"
) {
    auto state = std::make_shared<BootstrapThreadState>();

    App app;
    app.add_plugin<AssetsPlugin>()
        .add_resource_as<GraphicsBackendBootstrap>(
            BoxedGraphicsBackendBootstrap(
                std::make_unique<FakeGraphicsBootstrap>(state)
            )
        )
        .add_resource(GraphicsSurfaceSize {.width = 640, .height = 360});
    install_render_app(app);

    app.add_plugin<RenderingPlugin>();
    REQUIRE_THROWS_AS(app.finish(), std::runtime_error);
    REQUIRE(state->initialized_on == std::thread::id {});
}

TEST_CASE(
    "RenderingPlugin rejects mismatched bootstrap and runtime capabilities",
    "[rendering][plugin][graphics-runtime][bootstrap]"
) {
    auto state = std::make_shared<BootstrapThreadState>();
    auto bootstrap = std::make_unique<FakeGraphicsBootstrap>(
        state,
        GraphicsBackendCapabilities {
            .backend = GraphicsBackendKind::OpenGL,
        }
    );

    App app;
    app.add_plugin<AssetsPlugin>()
        .add_resource_as<GraphicsBackendBootstrap>(
            BoxedGraphicsBackendBootstrap(std::move(bootstrap))
        )
        .add_resource(GraphicsSurfaceSize {.width = 640, .height = 360});

    app.add_plugin<RenderingPlugin>();
    REQUIRE_THROWS_AS(app.finish(), std::runtime_error);
    REQUIRE(state->initialized_on != std::thread::id {});
}
