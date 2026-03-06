#version 430

out Vertex
{
    vec2 texCoord;
    vec3 normal;
};

layout(row_major, std140) uniform Mesh {
    mat4 world_from_local;
} mesh;

layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;

void main()
{
    gl_Position = mesh.world_from_local * vec4(Vertex_Position, 1.0f);

    normal = transpose(inverse(mat3(mesh.world_from_local))) * Vertex_Normal;
    texCoord = Vertex_Uv;
}
