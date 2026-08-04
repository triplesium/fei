#include "rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "core/camera.hpp"
#include "core/transform.hpp"
#include "core/transform_plugin.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_profile.hpp"
#include "graphics/backend.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "math/primitives.hpp"
#include "rendering/components.hpp"
#include "rendering/defaults.hpp"
#include "rendering/extract_resource.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_aabb.hpp"
#include "rendering/mesh/mesh_loader.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_app.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/resource_set_cache.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/shader_compiler.hpp"
#include "rendering/view.hpp"
#include "rendering/visibility.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace fei {

namespace {

std::filesystem::path default_shader_cache_root() {
#ifdef FEI_SHADER_CACHE_PATH
    return FEI_SHADER_CACHE_PATH;
#else
    return std::filesystem::current_path() / "build" / "cache" / "shaders";
#endif
}

void validate_backend_execution_mode(SubAppExecutionMode execution_mode) {
    if (execution_mode != SubAppExecutionMode::DedicatedThread) {
        throw std::runtime_error(
            "Graphics backends require a dedicated Render Worker"
        );
    }
}

void install_backend_render_app(App& app) {
    if (app.has_sub_app<RenderApp>()) {
        return;
    }
    if (app.has_resource<GraphicsBackendBootstrap>()) {
        install_render_app(app, [](SubApp render_app) {
            return std::make_unique<ThreadedRenderRunner>(
                std::move(render_app)
            );
        });
        return;
    }
    install_render_app(app);
}

void initialize_graphics_backend(App& app) {
    if (!app.has_resource<GraphicsBackendBootstrap>()) {
        return;
    }
    auto& runner = app.sub_app_runner<RenderApp>();
    auto& bootstrap = app.resource<GraphicsBackendBootstrap>();
    const auto bootstrap_capabilities = bootstrap.capabilities();
    validate_backend_execution_mode(runner.execution_mode());
    runner.run_on_execution_thread(
        [&bootstrap, bootstrap_capabilities](SubApp& render_app) {
            auto runtime = bootstrap.initialize();
            if (!runtime) {
                throw std::runtime_error(
                    "GraphicsBackendBootstrap returned an empty runtime"
                );
            }

            const auto capabilities = runtime->capabilities();
            if (capabilities != bootstrap_capabilities) {
                throw std::runtime_error(
                    "GraphicsBackendBootstrap and GraphicsRuntime capabilities "
                    "do not match"
                );
            }
            auto presentation_target = runtime->presentation_target();
            render_app.add_resource_as<GraphicsRuntime>(
                BoxedGraphicsRuntime(std::move(runtime))
            );
            auto& installed_runtime = render_app.resource<GraphicsRuntime>();
            render_app.add_readonly_resource_ref(installed_runtime.device())
                .add_resource(capabilities);
            if (presentation_target) {
                render_app.add_resource(
                    MainSwapchain {.swapchain = std::move(presentation_target)}
                );
            }
        }
    );
}

} // namespace

void process_pipelines(ResRW<PipelineCache> pipeline_cache) {
    pipeline_cache->process_queued_pipelines();
}

void begin_render_resource_set_cache(ResRW<RenderResourceSetCache> cache) {
    cache->begin_frame();
}

void resize_graphics_runtime(
    ResRO<GraphicsSurfaceSize> surface_size,
    ResRW<GraphicsRuntime> runtime
) {
    runtime->resize(surface_size->width, surface_size->height);
}

void present_graphics_runtime(Optional<ResRO<GraphicsRuntime>> runtime) {
    if (runtime) {
        (*runtime)->present();
    }
}

void shutdown_graphics_runtime(World& world) {
    if (world.has_resource<RenderResourceSetCache>()) {
        world.resource<RenderResourceSetCache>().clear();
    }
    if (world.has_resource<GraphicsRuntime>()) {
        static_cast<const World&>(world).resource<GraphicsRuntime>().flush();
    }
}

void RenderingPlugin::setup(App& app) {
    if (app.has_resource<GraphicsRuntime>() ||
        app.has_resource<GraphicsDevice>() ||
        app.has_resource<MainSwapchain>()) {
        throw std::runtime_error(
            "RenderingPlugin no longer accepts graphics resources in the "
            "Main World; install GraphicsBackendBootstrap instead"
        );
    }

    install_backend_render_app(app);
    initialize_graphics_backend(app);

    if (!app.has_plugin<TransformPlugin>()) {
        app.add_plugin<TransformPlugin>();
    }

    add_extract_component<Camera3d>(app);
    add_extract_component<GlobalTransform3d>(app);
    add_extract_component<Mesh3d>(app);
    add_extract_component<Aabb>(app);
    auto& render_app = app.sub_app<RenderApp>();
    if (!render_app.has_resource<GraphicsDevice>()) {
        throw std::runtime_error(
            "RenderingPlugin requires GraphicsBackendBootstrap or a "
            "GraphicsDevice installed directly in the Render World"
        );
    }
    if (render_app.has_resource<GraphicsRuntime>() &&
        app.has_resource<GraphicsSurfaceSize>()) {
        add_extract_resource<GraphicsSurfaceSize>(app);
        render_app.add_systems(
            RenderPrepare,
            resize_graphics_runtime | main_thread()
        );
    }

    const auto& graphics_device =
        static_cast<const SubApp&>(render_app).resource<GraphicsDevice>();

    render_app.add_shutdown(shutdown_graphics_runtime);

    app.resource<AssetServer>().emplace_source<ShaderAssetSource>();
    render_app.add_resource(SlangLibraryShaderCompiler {});
    render_app.add_resource(ShaderVariantCompiler(
        render_app.resource<SlangLibraryShaderCompiler>(),
        RuntimeShaderCompilerConfig {
            .cache_root = default_shader_cache_root(),
        }
    ));

    render_app
        .configure_sets(
            RenderUpdate,
            chain(
                RenderingSystems::PrepareAssets(),
                RenderingSystems::PrepareResources(),
                RenderingSystems::CheckVisibility(),
                RenderingSystems::Queue(),
                RenderingSystems::PreparePipelines(),
                RenderingSystems::Render(),
                RenderingSystems::BeginRender(),
                RenderingSystems::Prepass(),
                RenderingSystems::MainPass(),
                RenderingSystems::PostProcess(),
                RenderingSystems::Overlay(),
                RenderingSystems::Submit()
            )
        )
        .configure_sets(
            RenderUpdate,
            chain(
                RenderingSystems::PrepareView {}
                    .in_set<RenderingSystems::PrepareResources>(),
                RenderingSystems::UploadViewUniforms {}
                    .in_set<RenderingSystems::PrepareResources>()
            )
        );

    app.add_plugins(
        AssetPlugin<Shader, ShaderLoader> {},
        AssetPlugin<Mesh, MeshLoader> {},
        RenderAssetPlugin<Image, GpuImage, GpuImageAdapter> {},
        RenderAssetPlugin<Mesh, GpuMesh, GpuMeshAdapter> {},
        RenderingDefaultsPlugin {}
    );

    render_app.add_resource(PipelineCache(graphics_device))
        .add_resource<RenderFrameContext>()
        .add_resource(RenderQueue {})
        .add_resource<RenderResourceSetCache>()
        .add_resource<ViewUniforms>()
        .add_resource(ShaderCache(
            graphics_device,
            &render_app.resource<ShaderVariantCompiler>()
        ))
        .add_resource<MeshUniforms>()
        .add_resource<ViewVisibleEntities>();

    app.add_systems(PostUpdate, compute_mesh_aabb);

    render_app.add_systems(RenderExtract, FEI_NAMED_SYSTEM(extract_shaders))
        .add_systems(
            RenderUpdate,
            chain(init_camera_view_uniform, prepare_camera_view_uniform) |
                in_set<RenderingSystems::PrepareResources>() |
                in_set<RenderingSystems::PrepareView>(),
            prepare_mesh_uniforms |
                in_set<RenderingSystems::PrepareResources>(),
            upload_view_uniforms |
                in_set<RenderingSystems::PrepareResources>() |
                in_set<RenderingSystems::UploadViewUniforms>()
        )
        .add_systems(
            RenderUpdate,
            check_mesh_visibility | in_set<RenderingSystems::CheckVisibility>()
        )
        .add_systems(
            RenderUpdate,
            process_pipelines | in_set<RenderingSystems::PreparePipelines>()
        )
        .add_systems(
            RenderUpdate,
            chain(
                FEI_NAMED_SYSTEM(begin_render_resource_set_cache),
                FEI_NAMED_SYSTEM(begin_render_frame),
                FEI_NAMED_SYSTEM(flush_render_queue)
            ) | in_set<RenderingSystems::BeginRender>(),
            FEI_NAMED_SYSTEM(submit_render_frame) |
                in_set<RenderingSystems::Submit>()
        )
        .add_systems(RenderLast, present_graphics_runtime | main_thread());
}

} // namespace fei
