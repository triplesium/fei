struct VertexInput {
    [vk::location(0)] float3 position : POSITION;
    [vk::location(2)] float2 uv : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.tex_coords = input.uv;
    output.position = float4(input.position, 1.0);
    return output;
}
