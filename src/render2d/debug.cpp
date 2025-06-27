#include "debug.hpp"
#include "graphics/device.hpp"
#include "graphics/shader.hpp"

namespace fei {

const char* debug_vertex_shader = R"(#version 330 core
layout (location = 0) in vec2 vVertex;
layout (location = 1) in vec4 vColor;

out vec4 fColor;

void main()
{
    fColor = vColor;
    gl_Position = vec4(vVertex, 0.0, 1.0);
}
)";

const char* debug_fragment_shader = R"(#version 330 core
in vec4 fColor;
out vec4 oColor;

void main()
{
    oColor = fColor;
}
)";

void Debug::line(Vector2 from, Vector2 to, Color4F color) {
    data.push_back({from, color});
    data.push_back({to, color});
}

void Debug::rect(Rect rect, Color4F color) {
    line(rect.min, {rect.max.x, rect.min.y}, color);
    line({rect.max.x, rect.min.y}, rect.max, color);
    line(rect.max, {rect.min.x, rect.max.y}, color);
    line({rect.min.x, rect.max.y}, rect.min, color);
}

void Debug::circle(Vector2 center, float radius, int segments, Color4F color) {
    float step = 2.0f * PI / segments;
    for (int i = 0; i < segments; ++i) {
        float angle = i * step;
        float next_angle = (i + 1) * step;
        line(
            center + Vector2 {radius * cos(angle), radius * sin(angle)},
            center +
                Vector2 {radius * cos(next_angle), radius * sin(next_angle)},
            color
        );
    }
}

void Debug::clear() {
    data.clear();
}

void setup_debug(Res<Debug> debug) {
    auto* device = RenderDevice::instance();
    Shader* vert =
        device->create_shader(ShaderStage::Vertex, debug_vertex_shader);
    Shader* frag =
        device->create_shader(ShaderStage::Fragment, debug_fragment_shader);
    debug->program = device->create_program(*vert, *frag);
    delete vert;
    delete frag;
    debug->buffer =
        device->create_buffer(BufferType::Vertex, BufferUsage::Dynamic);
}

void draw_line_clear(Res<Debug> debug) {
    debug->clear();
}

void draw_line_update(
    Res<Debug> debug,
    Res<RenderResource> render,
    Query<Camera, Transform2D> q_camera
) {
    if (q_camera.empty())
        return;
    auto* device = render->device;
    auto* draw_list = render->draw_list;
    auto [camera, camera_transform] = q_camera.first();
    Matrix4x4 proj = camera.projection();
    Matrix4x4 view = camera_transform.model_matrix().inverse_affine();

    auto do_vertex_transform =
        [](Vector2& vert, const Matrix4x4& view, const Matrix4x4& proj) {
            Vector4 vert_homo {vert.x, vert.y, 0.0f, 1.0f};
            vert_homo = proj * view * vert_homo;
            vert = {vert_homo.x, vert_homo.y};
        };

    for (auto& v2f_c4f : debug->data) {
        auto& vertices = v2f_c4f.vertices;
        do_vertex_transform(vertices, view, proj);
    }

    debug->buffer->update_data(
        reinterpret_cast<const std::byte*>(debug->data.data()),
        debug->data.size() * sizeof(V2F_C4F)
    );

    RenderPipelineDescriptor desc {
        .program = debug->program,
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
                  }},
             .stride = sizeof(V2F_C4F)},
        .render_primitive = RenderPrimitive::Lines,
        .rasterization_state = {},
    };
    auto pipeline = device->create_render_pipeline(desc);
    draw_list->bind_render_pipeline(pipeline);
    draw_list->bind_vertex_buffer(debug->buffer);
    draw_list->draw(0, debug->data.size());
    delete pipeline;
}

void DebugPlugin::setup(App& app) {
    app.add_resource<Debug>();
    app.add_system(StartUp, setup_debug);
    app.add_system(PreUpdate, draw_line_clear);
    app.add_system(RenderUpdate, draw_line_update);
}

} // namespace fei
