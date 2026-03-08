#version 450 core
in vec2 Frag_TexCoords;

uniform sampler2D source;

layout(row_major, std140) uniform Blur {
    vec2 direction;
    int type;
};

out vec4 Out_Color;

vec4 blur13(sampler2D image, vec2 uv, vec2 direction) {
    vec4 color = vec4(0.0);
    vec2 off1 = vec2(1.411764705882353) * direction;
    vec2 off2 = vec2(3.2941176470588234) * direction;
    vec2 off3 = vec2(5.176470588235294) * direction;
    color += texture(image, uv) * 0.1964825501511404;
    color += texture(image, uv + off1) * 0.2969069646728344;
    color += texture(image, uv - off1) * 0.2969069646728344;
    color += texture(image, uv + off2) * 0.09447039785044732;
    color += texture(image, uv - off2) * 0.09447039785044732;
    color += texture(image, uv + off3) * 0.010381362401148057;
    color += texture(image, uv - off3) * 0.010381362401148057;
    return color;
}

vec4 blur5(sampler2D image, vec2 uv, vec2 direction) {
    vec4 color = vec4(0.0);
    vec2 off1 = vec2(1.3333333333333333) * direction;
    color += texture(image, uv) * 0.29411764705882354;
    color += texture(image, uv + off1) * 0.35294117647058826;
    color += texture(image, uv - off1) * 0.35294117647058826;
    return color;
}

vec4 blur9(sampler2D image, vec2 uv, vec2 direction) {
    vec4 color = vec4(0.0);
    vec2 off1 = vec2(1.3846153846) * direction;
    vec2 off2 = vec2(3.2307692308) * direction;
    color += texture(image, uv) * 0.2270270270;
    color += texture(image, uv + off1) * 0.3162162162;
    color += texture(image, uv - off1) * 0.3162162162;
    color += texture(image, uv + off2) * 0.0702702703;
    color += texture(image, uv - off2) * 0.0702702703;
    return color;
}

void main() {
    if (type == 1)
        Out_Color = blur5(source, Frag_TexCoords, direction);
    else if (type == 2)
        Out_Color = blur9(source, Frag_TexCoords, direction);
    else if (type == 3)
        Out_Color = blur13(source, Frag_TexCoords, direction);
    else
        Out_Color = blur5(source, Frag_TexCoords, direction);
};
