#include "render2d/render.hpp"
#include "GLFW/glfw3.h"

namespace fei {

void render_setup(Res<RenderResource> render_res, Res<Window> win_res) {
    auto device = RenderDevice::instance();
    render_res->device = device;
    render_res->color_tex = device->create_texture2d(TextureDescriptor {
        .texture_type = TextureType::Texture2D,
        .texture_format = PixelFormat::RGBA8888,
        .texture_usage = TextureUsage::RenderTarget,
        .width = win_res->width,
        .height = win_res->height,
        .sampler_descriptor = {},
        .data = nullptr,
    });
    render_res->framebuffer = device->create_framebuffer(FramebufferDescriptor {
        .color_attachments = {Attachment {.texture = render_res->color_tex}},
        .depth_attachment = {nullptr},
    });
    render_res->draw_list = device->create_draw_list();
}

void render_start(Res<RenderResource> render_res, Res<Window> win_res) {
    auto draw_list = render_res->draw_list;
    render_res->color_tex
        ->update_data(nullptr, win_res->width, win_res->height);
    draw_list->begin(RenderPassDescriptor {
        .framebuffer = render_res->framebuffer,
        .clear_color = true,
        .clear_color_value = render_res->clear_color_value,
    });
    draw_list->set_viewport(0, 0, win_res->width, win_res->height);
}

void render_end(Res<RenderResource> render_res, Res<Window> win_res) {
    auto draw_list = render_res->draw_list;
    draw_list->end();
    // NOTE: temporary solution, move it to window system
    glfwSwapBuffers(win_res->glfw_window);
}

void RenderPlugin::setup(App& app) {
    app.add_resource<RenderResource>();
    app.add_system(StartUp, render_setup);
    app.add_system(RenderFirst, render_start);
    app.add_system(RenderLast, render_end);
}

} // namespace fei
