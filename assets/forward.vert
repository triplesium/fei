#version 450 core
layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;

layout(binding = 1, row_major, std140) uniform View {
    mat4 view_projection;
};

layout(binding = 2, row_major, std140) uniform Mesh {
    mat4 model;
};

out vec2 Frag_TexCoords;

void main()
{
    Frag_TexCoords = Vertex_Uv;
    gl_Position = view_projection * model * vec4(Vertex_Position, 1.0);
}
