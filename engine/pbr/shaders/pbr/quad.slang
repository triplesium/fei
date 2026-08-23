struct VertexInput {
    [vk::location(0)] float3 position : POSITION0;
    [vk::location(2)] float2 uv : TEXCOORD2;
};

struct VertexOutput {
    float4 position : SV_Position;
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
#if ETS_SHADER_TARGET_WGSL
    output.tex_coords = float2(input.uv.x, 1.0 - input.uv.y);
#else
    output.tex_coords = input.uv;
#endif
    output.position = float4(input.position, 1.0);
    return output;
}
