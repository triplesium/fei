#version 450 core
layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;
layout (location = 4) in vec3 Vertex_Tangent;

layout(row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

layout(row_major, std140) uniform Mesh {
    mat4 world_from_local;
} mesh;

out vec3 Frag_Position;
out vec3 Frag_Normal;
out vec2 Frag_TexCoords;
out mat3 Frag_TBN;

void main()
{
    Frag_Position = vec3(mesh.world_from_local * vec4(Vertex_Position, 1.0));
    mat3 normal_matrix = mat3(transpose(inverse(mesh.world_from_local)));
    Frag_Normal = normal_matrix * Vertex_Normal;
    Frag_TexCoords = Vertex_Uv;

    vec3 T = normalize(normal_matrix * Vertex_Tangent);
    vec3 N = normalize(Frag_Normal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    Frag_TBN = mat3(T, B, N);

    gl_Position = view.clip_from_world * vec4(Frag_Position, 1.0);
}
