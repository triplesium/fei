#include "render2d/sprite.hpp"
#include "graphics/device.hpp"
#include "graphics/shader.hpp"
#include "math/matrix.hpp"

#include <array>

namespace fei {

const char* sprite_vertex_shader = R"(#version 330 core
layout (location = 0) in vec2 vVertex;
layout (location = 1) in vec4 vColor;
layout (location = 2) in vec2 vTexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec2 fTexCoords;
out vec4 fColor;

void main()
{
    fTexCoords = vTexCoords;
    fColor = vColor;
    gl_Position = proj * view * model * vec4(vVertex, 0.0, 1.0);
}
)";

const char* sprite_fragment_shader = R"(#version 330 core
in vec2 fTexCoords;
in vec4 fColor;
out vec4 oColor;

uniform sampler2D image;

void main()
{
    oColor = fColor * texture(image, fTexCoords);
}
)";

void sprite_renderer_setup(Res<SpriteRendererResource> res) {
    auto device = RenderDevice::instance();
    res->vertex_buffer =
        device->create_buffer(BufferType::Vertex, BufferUsage::Dynamic);

    Shader* vert =
        device->create_shader(ShaderStage::Vertex, sprite_vertex_shader);
    Shader* frag =
        device->create_shader(ShaderStage::Fragment, sprite_fragment_shader);
    res->program = device->create_program(*vert, *frag);
    delete vert;
    delete frag;

    RenderPipelineDescriptor desc {
        .program = res->program,
        .vertex_layout =
            {.attributes =
                 {VertexAttribute {
                      .location = 0,
                      .offset = 0,
                      .format = VertexFormat::Float2,
                      .normalized = false,
                  },
                  VertexAttribute {
                      .location = 1,
                      .offset = sizeof(Vector2),
                      .format = VertexFormat::Float4,
                      .normalized = false,
                  },
                  VertexAttribute {
                      .location = 2,
                      .offset = sizeof(Vector2) + sizeof(Color4F),
                      .format = VertexFormat::Float2,
                      .normalized = false,
                  }},
             .stride = sizeof(V2F_C4F_T2F)},
        .render_primitive = RenderPrimitive::Triangles,
        .rasterization_state = {},
    };
    res->pipeline = device->create_render_pipeline(desc);
}

void render_sprite(
    Res<SpriteRendererResource> sprite_res,
    Res<RenderResource> render_res,
    Res<AssetServer> asset_server,
    Query<Sprite, Transform2D> q_sprite,
    Query<Camera, Transform2D> q_camera
) {
    if (q_camera.empty()) {
        return;
    }
    auto* draw_list = render_res->draw_list;
    auto [camera, camera_transform] = q_camera.first();
    Matrix4x4 proj = camera.projection();
    Matrix4x4 view = camera_transform.model_matrix().inverse_affine();

    sprite_res->pipeline->set_uniform("model", Matrix4x4::Identity);
    sprite_res->pipeline->set_uniform("view", view);
    sprite_res->pipeline->set_uniform("proj", proj);

    for (auto [sprite, transform] : q_sprite) {
        auto& texture = sprite.texture.get().value();

        V2F_C4F_T2F_Quad quad;
        quad.bl.vertices = {0.0f, 0.0f};
        quad.br.vertices = {1.0f, 0.0f};
        quad.tl.vertices = {0.0f, 1.0f};
        quad.tr.vertices = {1.0f, 1.0f};

        for (auto& x : std::vector {&quad.tl, &quad.tr, &quad.bl, &quad.br}) {
            x->color = sprite.color;
        }

        quad.bl.tex_coords = {0.0f, 0.0f};
        quad.br.tex_coords = {1.0f, 0.0f};
        quad.tl.tex_coords = {0.0f, 1.0f};
        quad.tr.tex_coords = {1.0f, 1.0f};

        std::array<V2F_C4F_T2F, 6> buffer =
            {quad.tl, quad.br, quad.bl, quad.tl, quad.tr, quad.br};

        sprite_res->vertex_buffer->update_data(
            reinterpret_cast<const std::byte*>(buffer.data()),
            6 * sizeof(V2F_C4F_T2F)
        );

        Vector3 size = {(float)texture.width(), (float)texture.height(), 1.0f};
        Matrix4x4 model = transform.model_matrix() *
                          scale(size.x, size.y, 1.0f) *
                          translate(-sprite.anchor.x, -sprite.anchor.y, 0.0f);

        sprite_res->pipeline->set_uniform("model", model);

        draw_list->bind_render_pipeline(sprite_res->pipeline);
        draw_list->bind_vertex_buffer(sprite_res->vertex_buffer);
        draw_list->bind_texture(&texture);
        draw_list->draw(0, buffer.size());
    }
}

void SpritePlugin::setup(App& app) {
    app.add_resource(SpriteRendererResource {});
    app.add_system(StartUp, sprite_renderer_setup);
    app.add_system(RenderUpdate, render_sprite);
}

} // namespace fei
