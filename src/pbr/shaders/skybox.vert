#version 450 core
layout (location = 0) in vec3 Vertex_Position;

layout(row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

out vec3 Frag_TexCoords;

void main()
{
    Frag_TexCoords = Vertex_Position;
    vec4 pos = view.clip_from_view * mat4(mat3(view.view_from_world)) * vec4(Vertex_Position, 1.0);
    gl_Position = pos.xyww;
}
