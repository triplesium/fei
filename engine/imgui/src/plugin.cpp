#include "imgui/plugin.hpp"

#include "app/app.hpp"
#include "asset/embed.hpp"
#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_profile.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "imgui/renderer.hpp"
#include "imgui/texture.hpp"
#include "rendering/defaults.hpp"
#include "rendering/extract_resource.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/shader_cache.hpp"
#include "window/window.hpp"

#include <cstddef>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <limits>

EMBED(Cousine_Regular_ttf, "Cousine-Regular.ttf");

namespace fei {

template<>
struct ExtractResource<ExtractedImGuiFrame> {
    using Source = PendingImGuiFrame;

    static ExtractedImGuiFrame extract_resource(const Source& source) {
        return ExtractedImGuiFrame {.snapshot = source.snapshot};
    }
};

template<>
struct ExtractResource<ExtractedImGuiImages> {
    using Source = ImGuiImages;

    static ExtractedImGuiImages extract_resource(const Source& source) {
        return source.extract();
    }
};

namespace {

struct ImGuiLifecycle {
    bool platform_initialized {false};
};

void setup_imgui_platform(
    ResRO<Window> window,
    ResRO<ImGuiPluginConfig> plugin_config,
    ResRW<ImGuiLifecycle> lifecycle
) {
    IMGUI_CHECKVERSION();
    if (ImGui::GetCurrentContext()) {
        fatal("ImGuiPlugin requires exclusive ownership of the ImGui context");
    }
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    if (plugin_config->docking) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    auto reader = EmbeddedAssets::get("Cousine-Regular.ttf").reader();
    if (reader.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        fatal("Embedded ImGui font exceeds ImGui's size limit");
    }
    void* font_data = static_cast<void*>(const_cast<std::byte*>(reader.data()));
    const int font_size = static_cast<int>(reader.size());
    for (float size : {20.0f, 16.0f, 14.0f}) {
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        if (!io.Fonts
                 ->AddFontFromMemoryTTF(font_data, font_size, size, &config)) {
            fatal("ImGuiPlugin failed to load the embedded Cousine font");
        }
    }

    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOther(window->glfw_window, true)) {
        fatal("ImGuiPlugin failed to initialize the GLFW platform backend");
    }
    lifecycle->platform_initialized = true;
    io.BackendRendererName = "fei-imgui";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
}

void setup_imgui_renderer(
    ResRO<GraphicsDevice> device,
    ResRW<ImGuiTextureRegistry> texture_registry,
    ResRW<ImGuiRenderer> renderer
) {
    renderer->initialize(*device, *texture_registry);
}

void begin_imgui_frame(ResRW<ImGuiInputCapture> capture) {
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    capture->mouse = io.WantCaptureMouse;
    capture->keyboard = io.WantCaptureKeyboard;
    capture->text = io.WantTextInput;
}

void capture_imgui_frame(ResRW<PendingImGuiFrame> pending_frame) {
    pending_frame->snapshot = capture_imgui_frame_snapshot();
}

void prepare_imgui_pipeline(
    ResRW<ImGuiRenderer> renderer,
    ResRW<ShaderCache> shader_cache,
    ResRW<PipelineCache> pipeline_cache,
    ResRO<MainSwapchain> main_swapchain
) {
    if (!main_swapchain->swapchain) {
        return;
    }
    renderer->prepare_pipeline(
        *shader_cache,
        *pipeline_cache,
        *main_swapchain->swapchain
    );
}

void sync_imgui_images(
    ResRO<GraphicsDevice> device,
    ResRO<ExtractedImGuiImages> extracted_images,
    ResRO<RenderAssets<GpuImage>> gpu_images,
    ResRO<RenderingDefaults> defaults,
    ResRW<ImGuiTextureRegistry> texture_registry
) {
    texture_registry
        ->sync_images(*device, *extracted_images, *gpu_images, *defaults);
}

void render_imgui_overlay(
    ResRO<GraphicsDevice> device,
    ResRW<PipelineCache> pipeline_cache,
    ResRW<RenderFrameContext> frame_context,
    ResRO<MainSwapchain> main_swapchain,
    ResRW<ImGuiTextureRegistry> texture_registry,
    ResRW<ImGuiRenderer> renderer,
    ResRO<ExtractedImGuiFrame> extracted_frame
) {
    static const ImGuiFrameSnapshot empty_frame;
    renderer->render(
        *device,
        *pipeline_cache,
        *frame_context,
        *main_swapchain,
        *texture_registry,
        extracted_frame->snapshot ? *extracted_frame->snapshot : empty_frame
    );
}

} // namespace

void ImGuiPlugin::setup(App& app) {
    if (!app.has_resource<Window>()) {
        fatal(
            "ImGuiPlugin requires Window; install a GLFW graphics plugin first"
        );
    }
    if (!app.has_plugin<RenderingPlugin>()) {
        fatal("ImGuiPlugin requires RenderingPlugin to be installed first");
    }
    auto& render_app = app.sub_app<RenderApp>();
    if (!render_app.has_resource<GraphicsDevice>()) {
        fatal("ImGuiPlugin requires GraphicsDevice in the Render World");
    }
    if (!render_app.has_resource<MainSwapchain>()) {
        fatal("ImGuiPlugin requires MainSwapchain in the Render World");
    }

    add_extract_resource<Window>(app);
    add_extract_resource<ExtractedImGuiFrame>(app);
    add_extract_resource<ExtractedImGuiImages>(app);
    app.add_resource(m_config)
        .add_resource(ImGuiInputCapture {})
        .add_resource(ImGuiImages {})
        .add_resource(ImGuiRenderTextures {})
        .add_resource(ImGuiLifecycle {})
        .add_resource(PendingImGuiFrame {})
        .add_systems(StartUp, setup_imgui_platform | main_thread())
        .add_systems(PreUpdate, begin_imgui_frame | main_thread())
        .add_systems(RenderLast, capture_imgui_frame | main_thread());
    render_app.add_resource(ImGuiTextureRegistry {})
        .add_resource(ImGuiRenderer {})
        .add_shutdown([](World& world) {
            if (world.has_resource<ImGuiRenderer>() &&
                world.has_resource<ImGuiTextureRegistry>()) {
                world.resource<ImGuiRenderer>().shutdown(
                    world.resource<ImGuiTextureRegistry>()
                );
            }
        })
        .add_systems(
            RenderStartup,
            FEI_NAMED_SYSTEM(setup_imgui_renderer) | main_thread()
        )
        .add_systems(
            RenderUpdate,
            FEI_NAMED_SYSTEM(prepare_imgui_pipeline) |
                in_set<RenderingSystems::PrepareResources>(),
            FEI_NAMED_SYSTEM(sync_imgui_images) |
                in_set<RenderingSystems::PrepareResources>()
        )
        .add_systems(
            RenderUpdate,
            FEI_NAMED_SYSTEM(render_imgui_overlay) |
                in_set<RenderingSystems::Overlay>() | main_thread()
        );
}

void ImGuiPlugin::cleanup(App& app) noexcept {
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    if (app.has_resource<ImGuiLifecycle>() &&
        app.resource<ImGuiLifecycle>().platform_initialized) {
        ImGui_ImplGlfw_Shutdown();
        app.resource<ImGuiLifecycle>().platform_initialized = false;
    }
    ImGui::DestroyContext();
}

} // namespace fei
