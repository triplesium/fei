#include "imgui/plugin.hpp"

#include "app/app.hpp"
#include "asset/embed.hpp"
#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "imgui/renderer.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/shader_cache.hpp"
#include "window/window.hpp"

#include <cstddef>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <limits>

EMBED(Cousine_Regular_ttf, "Cousine-Regular.ttf");

namespace fei {

namespace {

struct ImGuiLifecycle {
    bool platform_initialized {false};
};

void setup_imgui(
    ResRO<Window> window,
    ResRO<GraphicsDevice> device,
    ResRW<ImGuiTextureRegistry> texture_registry,
    ResRW<ImGuiRenderer> renderer,
    ResRW<ImGuiLifecycle> lifecycle
) {
    IMGUI_CHECKVERSION();
    if (ImGui::GetCurrentContext()) {
        fatal("ImGuiPlugin requires exclusive ownership of the ImGui context");
    }
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    auto reader = EmbededAssets::get("Cousine-Regular.ttf").reader();
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

void render_imgui_overlay(
    ResRO<GraphicsDevice> device,
    ResRW<PipelineCache> pipeline_cache,
    ResRW<RenderFrameContext> frame_context,
    ResRO<MainSwapchain> main_swapchain,
    ResRW<ImGuiTextureRegistry> texture_registry,
    ResRW<ImGuiRenderer> renderer
) {
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    renderer->render(
        *device,
        *pipeline_cache,
        *frame_context,
        *main_swapchain,
        *texture_registry
    );
}

} // namespace

void ImGuiPlugin::setup(App& app) {
    if (!app.has_resource<Window>()) {
        fatal(
            "ImGuiPlugin requires Window; install a GLFW graphics plugin first"
        );
    }
    if (!app.has_resource<GraphicsDevice>()) {
        fatal("ImGuiPlugin requires GraphicsDevice");
    }
    if (!app.has_resource<MainSwapchain>()) {
        fatal("ImGuiPlugin requires MainSwapchain");
    }
    if (!app.has_plugin<RenderingPlugin>()) {
        fatal("ImGuiPlugin requires RenderingPlugin to be installed first");
    }

    app.add_resource(ImGuiTextureRegistry {})
        .add_resource(ImGuiRenderer {})
        .add_resource(ImGuiInputCapture {})
        .add_resource(ImGuiLifecycle {})
        .add_systems(StartUp, setup_imgui | main_thread())
        .add_systems(PreUpdate, begin_imgui_frame | main_thread())
        .add_systems(
            RenderUpdate,
            prepare_imgui_pipeline |
                in_set<RenderingSystems::PrepareResources>()
        )
        .add_systems(
            RenderUpdate,
            render_imgui_overlay | in_set<RenderingSystems::Overlay>() |
                main_thread()
        );
}

void ImGuiPlugin::cleanup(App& app) noexcept {
    if (app.has_resource<ImGuiRenderer>() &&
        app.has_resource<ImGuiTextureRegistry>()) {
        app.resource<ImGuiRenderer>().shutdown(
            app.resource<ImGuiTextureRegistry>()
        );
    }
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
