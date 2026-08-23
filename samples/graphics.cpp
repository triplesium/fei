#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "core/plugin.hpp"
#include "core/text.hpp"
#include "core/time.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/backend.hpp"
#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/swapchain.hpp"
#include "graphics/texture.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "math/common.hpp"
#include "math/matrix.hpp"
#include "rendering/extract_resource.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "rendering/render_asset.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

using namespace ets;

struct Renderer {
    std::shared_ptr<Buffer> vertex_buffer;
    std::shared_ptr<Buffer> index_buffer;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<Sampler> sampler;
    std::shared_ptr<ResourceSet> resource_set;
};

struct GraphicsSampleInput {
    Handle<Image> image;
    Handle<TextAsset> vertex_shader_asset;
    Handle<TextAsset> fragment_shader_asset;
    std::string vertex_shader;
    std::string fragment_shader;
};

void request_sample_assets(
    ResRW<AssetServer> asset_server,
    ResRW<GraphicsSampleInput> input
) {
    input->image = asset_server->load<Image>("awesomeface.png");
    input->vertex_shader_asset = asset_server->load<TextAsset>("forward.vert");
    input->fragment_shader_asset =
        asset_server->load<TextAsset>("forward.frag");
}

void resolve_sample_shaders(
    ResRO<Assets<TextAsset>> text_assets,
    ResRW<GraphicsSampleInput> input
) {
    if (!input->vertex_shader.empty() && !input->fragment_shader.empty()) {
        return;
    }
    const auto vertex_shader = text_assets->get(input->vertex_shader_asset);
    const auto fragment_shader = text_assets->get(input->fragment_shader_asset);
    if (!vertex_shader || !fragment_shader) {
        return;
    }
    input->vertex_shader = vertex_shader->text();
    input->fragment_shader = fragment_shader->text();
}

struct alignas(16) Uniforms {
    Matrix4x4 model;
    Matrix4x4 view;
    Matrix4x4 projection;
    float color[3];
};

void setup_renderer(
    ResRO<GraphicsDevice> device,
    ResRO<MainSwapchain> main_swapchain,
    ResRW<Renderer> renderer,
    ResRO<GraphicsSampleInput> input,
    ResRW<RenderAssets<GpuImage>> images
) {
    if (renderer->pipeline) {
        return;
    }
    if (input->vertex_shader.empty() || input->fragment_shader.empty()) {
        return;
    }
    auto image = images->get(input->image);
    if (!image) {
        return;
    }

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
    renderer->vertex_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(vertices),
            .usages = BufferUsages::Vertex,
        }
    );
    device->update_buffer(
        renderer->vertex_buffer,
        0,
        vertices.data(),
        sizeof(vertices)
    );

    // Create index buffer
    std::array<uint16_t, 6> indices = {0, 1, 3, 1, 2, 3};
    renderer->index_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(indices),
            .usages = BufferUsages::Index,
        }
    );
    device->update_buffer(
        renderer->index_buffer,
        0,
        indices.data(),
        sizeof(indices)
    );

    // Create uniform buffer
    renderer->uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(Uniforms),
            .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
        }
    );

    renderer->texture = image->texture();
    auto vert_shader = device->create_shader_module(
        ShaderDescription {
            .stage = ShaderStages::Vertex,
            .source = input->vertex_shader,
        }
    );
    auto frag_shader = device->create_shader_module(
        ShaderDescription {
            .stage = ShaderStages::Fragment,
            .source = input->fragment_shader,
        }
    );
    auto resource_layout = device->create_resource_layout(
        ResourceLayoutDescription {
            .elements = {
                ResourceLayoutElementDescription {
                    .binding = 0,
                    .kind = ResourceKind::UniformBuffer,
                    .stages = {ShaderStages::Vertex, ShaderStages::Fragment},
                },
                ResourceLayoutElementDescription {
                    .binding = 1,
                    .kind = ResourceKind::TextureReadOnly,
                    .stages = {ShaderStages::Fragment},
                },
            },
        }
    );
    renderer->pipeline = device->create_render_pipeline(
        RenderPipelineDescription {
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
            .output_description =
                main_swapchain->swapchain->framebuffer()->output_description(),
        }
    );
    renderer->resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resource_layout,
            .resources = {renderer->uniform_buffer, renderer->texture},
        }
    );
}

void render_update(
    ResRO<GraphicsDevice> device,
    ResRO<Renderer> renderer,
    ResRO<Time> time,
    ResRO<MainSwapchain> main_swapchain,
    ResRO<GraphicsSurfaceSize> surface_size
) {
    if (!renderer->pipeline) {
        return;
    }

    // Update uniform buffer
    Uniforms uniforms = {
        rotate_y(45.0f * time->elapsed_time() * DEG2RAD),
        translate(0.0f, 0.0f, -3.f),
        perspective(
            45.0f * DEG2RAD,
            static_cast<float>(surface_size->width) /
                static_cast<float>(surface_size->height),
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
    command_buffer
        ->set_viewport(0, 0, surface_size->width, surface_size->height);
    auto framebuffer = main_swapchain->swapchain->framebuffer();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.2f, 0.3f, 0.3f, 1.0f},
                    },
                },
            .framebuffer = framebuffer,
        }
    );
    command_buffer->set_render_pipeline(renderer->pipeline);
    command_buffer->set_vertex_buffer(renderer->vertex_buffer);
    command_buffer->set_index_buffer(
        renderer->index_buffer,
        IndexFormat::Uint16
    );
    command_buffer->set_resource_set(0, renderer->resource_set);
    command_buffer->draw_indexed(6);
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

int main() {
    App app;
    app.add_plugin<AssetsPlugin>()
        .add_plugin<OpenGLGlfwPlugin>()
        .add_plugin<CorePlugin>()
        .add_plugin<RenderingPlugin>();

    app.add_resource(GraphicsSampleInput {});
    add_extract_resource<GraphicsSampleInput>(app);
    add_extract_resource<Time>(app);
    app.add_systems(PreStartUp, request_sample_assets)
        .add_systems(Update, resolve_sample_shaders);
    app.sub_app<RenderApp>()
        .add_resource<Renderer>()
        .add_systems(
            RenderUpdate,
            setup_renderer | in_set<RenderingSystems::PrepareResources>()
        )
        .add_systems(
            RenderUpdate,
            render_update | in_set<RenderingSystems::MainPass>()
        );
    app.run();

    return 0;
}
