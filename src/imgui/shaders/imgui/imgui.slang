struct ImGuiFrame {
    float2 scale;
    float2 translate;
};

struct ImGuiVertexInput {
    [vk::location(0)] float2 position : POSITION;
    [vk::location(1)] float2 tex_coords : TEXCOORD0;
    [vk::location(2)] float4 color : COLOR0;
};

struct ImGuiFragmentInput {
    float4 position : SV_Position;
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
    [vk::location(1)] float4 color : COLOR0;
};

layout(set = 0, binding = 0) ConstantBuffer<ImGuiFrame> frame;
layout(set = 1, binding = 0) Texture2D<float4> image;
layout(set = 1, binding = 1) SamplerState image_sampler;

[shader("vertex")]
ImGuiFragmentInput vertex_main(ImGuiVertexInput input) {
    ImGuiFragmentInput output;
    output.position = float4(
        input.position * frame.scale + frame.translate,
        0.0,
        1.0
    );
    output.tex_coords = input.tex_coords;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragment_main(ImGuiFragmentInput input) : SV_Target0 {
    return image.Sample(image_sampler, input.tex_coords) * input.color;
}
