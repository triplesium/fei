#version 450 core
layout(location = 0) in vec3 Vertex_Position;
layout(location = 1) in vec3 Vertex_Normal;
layout(location = 2) in vec2 Vertex_Uv;

layout(set = 0, binding = 0, row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

layout(set = 1, binding = 0, row_major, std140) uniform Mesh {
    mat4 world_from_local;
} mesh;

layout(location = 0) out vec3 Frag_Position;
layout(location = 1) out vec3 Frag_Normal;
layout(location = 2) out vec2 Frag_TexCoords;
layout(location = 3) out vec4 Frag_LightSpacePosition;

void main()
{
    Frag_Position = vec3(mesh.world_from_local * vec4(Vertex_Position, 1.0));
    Frag_Normal = mat3(transpose(inverse(mesh.world_from_local))) * Vertex_Normal;
    Frag_TexCoords = Vertex_Uv;
    // Frag_LightSpacePosition = light.clip_from_world * vec4(Frag_Position, 1.0);
    gl_Position = view.clip_from_world * vec4(Frag_Position, 1.0);
}
