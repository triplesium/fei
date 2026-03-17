#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "core/plugin.hpp"
#include "core/text.hpp"
#include "core/time.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/opengl/plugin.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/texture.hpp"
#include "math/common.hpp"
#include "math/matrix.hpp"
#include "window/window.hpp"

#include <array>
#include <cstdint>
#include <glfw/glfw3.h>
#include <memory>
#include <print>

using namespace fei;

struct Renderer {
    std::shared_ptr<Buffer> vertex_buffer;
    std::shared_ptr<Buffer> index_buffer;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<Sampler> sampler;
    std::shared_ptr<ResourceSet> resource_set;
};

struct alignas(16) Uniforms {
    Matrix4x4 model;
    Matrix4x4 view;
    Matrix4x4 projection;
    float color[3];
};

void start_up(
    Res<AssetServer> asset_server,
    Commands commands,
    Res<GraphicsDevice> device,
    Res<Renderer> renderer,
    Res<Assets<TextAsset>> text_assets,
    Res<Assets<Image>> image_assets
) {
    // Create vertex buffer
    struct Vertex {
        float position[3];
        float uv[2];
    };
    std::array<Vertex, 4> vertices = {{
        {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
        {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}},
    }};
    renderer->vertex_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(vertices),
        .usages = BufferUsages::Vertex,
    });
    device->update_buffer(
        renderer->vertex_buffer,
        0,
        vertices.data(),
        sizeof(vertices)
    );

    // Create index buffer
    std::array<uint16_t, 6> indices = {0, 1, 3, 1, 2, 3};
    renderer->index_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(indices),
        .usages = BufferUsages::Index,
    });
    device->update_buffer(
        renderer->index_buffer,
        0,
        indices.data(),
        sizeof(indices)
    );

    // Create uniform buffer
    renderer->uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(Uniforms),
        .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
    });

    auto image_handle = asset_server->load<Image>("awesomeface.png");
    auto image = image_assets->get(image_handle);
    renderer->texture = device->create_texture(TextureDescription {
        .width = image->width(),
        .height = image->height(),
        .depth = image->channels(),
        .mip_level = 1,
        .layer = 0,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = TextureUsage::Sampled,
        .texture_type = TextureType::Texture2D,
    });
    device->update_texture(
        renderer->texture,
        image->data(),
        0,
        0,
        0,
        image->width(),
        image->height(),
        image->channels(),
        0,
        0
    );
    auto vert_shader_text = asset_server->load<TextAsset>("forward.vert");
    auto frag_shader_text = asset_server->load<TextAsset>("forward.frag");
    auto vert_shader = device->create_shader_module(ShaderDescription {
        .stage = ShaderStages::Vertex,
        .source = text_assets->get(vert_shader_text)->text(),
    });
    auto frag_shader = device->create_shader_module(ShaderDescription {
        .stage = ShaderStages::Fragment,
        .source = text_assets->get(frag_shader_text)->text(),
    });
    auto resource_layout =
        device->create_resource_layout(ResourceLayoutDescription {
            .elements =
                {
                    ResourceLayoutElementDescription {
                        .binding = 0,
                        .kind = ResourceKind::UniformBuffer,
                        .stages =
                            {ShaderStages::Vertex, ShaderStages::Fragment},
                    },
                    ResourceLayoutElementDescription {
                        .binding = 1,
                        .kind = ResourceKind::TextureReadOnly,
                        .stages = {ShaderStages::Fragment},
                    },
                },
        });
    renderer->pipeline =
        device->create_render_pipeline(RenderPipelineDescription {
            .blend_state = BlendStateDescription::SingleAlphaBlend,
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyGreaterEqual,
            .rasterizer_state = {},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts =
                        {
                            VertexLayoutDescription {
                                .attributes =
                                    {
                                        VertexAttributeDescription {
                                            .location = 0,
                                            .offset = 0,
                                            .format = VertexFormat::Float3,
                                        },
                                        VertexAttributeDescription {
                                            .location = 1,
                                            .offset = sizeof(float) * 3,
                                            .format = VertexFormat::Float2,
                                        },
                                    },
                                .stride = sizeof(Vertex),
                            },
                        },
                    .shaders = {vert_shader, frag_shader},
                },
            .resource_layouts = {resource_layout},
        });
    renderer->resource_set =
        device->create_resource_set(ResourceSetDescription {
            .layout = resource_layout,
            .resources = {renderer->uniform_buffer, renderer->texture},
        });
}

void render_start(WorldRef world) {}

void render_update(
    WorldRef world,
    Res<GraphicsDevice> device,
    Res<Renderer> renderer,
    Res<Time> time,
    Res<Window> win
) {
    // Update uniform buffer
    Uniforms uniforms = {
        rotate_y(45.0f * time->elapsed_time() * DEG2RAD),
        translate(0.0f, 0.0f, -3.f),
        perspective(
            45.0f * DEG2RAD,
            (float)win->width / (float)win->height,
            0.1f,
            100.0f
        ),
        {
            (std::sin(time->elapsed_time()) + 1.0f) / 2.0f,
            (std::cos(time->elapsed_time()) + 1.0f) / 2.0f,
            0.0f,
        },
    };
    device->update_buffer(
        renderer->uniform_buffer,
        0,
        &uniforms,
        sizeof(Uniforms)
    );

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->set_viewport(0, 0, win->width, win->height);
    command_buffer->set_framebuffer(device->main_framebuffer());
    command_buffer->clear_color(Color4F {0.2f, 0.3f, 0.3f, 1.0f});
    // command_buffer->clear_depth(1.f);
    command_buffer->set_render_pipeline(renderer->pipeline);
    command_buffer->set_vertex_buffer(renderer->vertex_buffer);
    command_buffer->set_index_buffer(
        renderer->index_buffer,
        IndexFormat::Uint16
    );
    command_buffer->set_resource_set(0, renderer->resource_set);
    command_buffer->draw_indexed(6);
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void render_end(WorldRef world, Res<GraphicsDevice> device, Res<Window> win) {
    glfwSwapBuffers(win->glfw_window);
}

int main() {
    App()
        .add_plugin<WindowPlugin>()
        .add_plugin<AssetsPlugin>()
        .add_plugin<OpenGLPlugin>()
        .add_plugin<CorePlugin>()
        .add_resource<Renderer>()
        .add_systems(PreStartUp, start_up)
        .add_systems(RenderStart, render_start)
        .add_systems(RenderUpdate, render_update)
        .add_systems(RenderEnd, render_end)
        .run();

    return 0;
}
