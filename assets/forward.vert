#version 450 core
layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;

layout(row_major, std140) uniform View {
    mat4 view_projection;
    vec3 view_position;
};

layout(row_major, std140) uniform Mesh {
    mat4 model;
};

layout(row_major, std140) uniform Light {
    mat4 light_view_projection;
    vec3 light_position;
    vec3 light_color;
};

out vec3 Frag_Position;
out vec3 Frag_Normal;
out vec2 Frag_TexCoords;
out vec4 Frag_LightSpacePosition;

void main()
{
    Frag_Position = vec3(model * vec4(Vertex_Position, 1.0));
    Frag_Normal = mat3(transpose(inverse(model))) * Vertex_Normal;
    Frag_TexCoords = Vertex_Uv;
    Frag_LightSpacePosition = light_view_projection * vec4(Frag_Position, 1.0);
    gl_Position = view_projection * vec4(Frag_Position, 1.0);
}
