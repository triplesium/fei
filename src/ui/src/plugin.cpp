#include "ui/plugin.hpp"

#include "app/app.hpp"
#include "asset/embed.hpp"
#include "asset/server.hpp"
#include "graphics/graphics_device.hpp"
#include "window/window.hpp"

#include <cstddef>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace fei {

void setup_imgui(ResRO<Window> window, ResRO<AssetServer> asset_server) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    auto reader = EmbededAssets::get("Cousine-Regular.ttf").reader();
    void* data = static_cast<void*>(const_cast<std::byte*>(reader.data()));
    int size = static_cast<int>(reader.size());
    io.Fonts->AddFontFromMemoryTTF(data, size, 20.0f);
    io.Fonts->AddFontFromMemoryTTF(data, size, 16.0f);
    io.Fonts->AddFontFromMemoryTTF(data, size, 14.0f);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window->glfw_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
}

void begin_imgui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void end_imgui(ResRO<GraphicsDevice> device) {
    ImGui::Render();
    device->flush();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UIPlugin::setup(App& app) {
    app.add_systems(StartUp, setup_imgui)
        .add_systems(RenderStart, begin_imgui | main_thread())
        .add_systems(RenderEnd, end_imgui | main_thread());
}

} // namespace fei
