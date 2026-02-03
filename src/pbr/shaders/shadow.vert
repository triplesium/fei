#version 450 core
layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;

layout(row_major, std140) uniform Light {
    mat4 clip_from_world;
    vec3 world_position;
    vec3 color;
} light;

layout(row_major, std140) uniform Mesh {
    mat4 world_from_local;
} mesh;

void main()
{
    gl_Position = light.clip_from_world * mesh.world_from_local * vec4(Vertex_Position, 1.0);
}
