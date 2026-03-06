#version 450 core
layout (location = 0) in vec3 Vertex_Position;
layout (location = 1) in vec3 Vertex_Normal;
layout (location = 2) in vec2 Vertex_Uv;

out vec2 Frag_TexCoords;

void main()
{
    Frag_TexCoords = Vertex_Uv;
    gl_Position = vec4(Vertex_Position, 1.0);
}
