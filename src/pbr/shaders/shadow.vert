#version 450 core
layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;

layout(row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

layout(row_major, std140) uniform Mesh {
    mat4 world_from_local;
} mesh;

layout(row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 emissive;
    vec3 specular;
    int flags;
} material;

out vec2 Frag_TexCoords;

void main()
{
    gl_Position = view.clip_from_world * mesh.world_from_local * vec4(Vertex_Position, 1.0);
    Frag_TexCoords = Vertex_Uv;
}
