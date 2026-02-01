#version 450 core
layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;

layout(row_major, std140) uniform Light {
    mat4 light_view_projection;
    vec3 light_position;
    vec3 light_color;
};

layout(row_major, std140) uniform Mesh {
    mat4 model;
};

void main()
{
    gl_Position = light_view_projection * model * vec4(Vertex_Position, 1.0);
}
