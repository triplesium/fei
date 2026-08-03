import rendering.fullscreen;

struct BlurUniform {
    float2 direction;
    int type;
};

layout(set = 0, binding = 0) ConstantBuffer<BlurUniform> blur;
layout(set = 0, binding = 1) Texture2D<float4> source;
layout(set = 0, binding = 2) SamplerState sampler;

float4 blur13(Texture2D<float4> image, float2 uv, float2 direction) {
    float4 color = float4(0.0);
    float2 off1 = float2(1.411764705882353) * direction;
    float2 off2 = float2(3.2941176470588234) * direction;
    float2 off3 = float2(5.176470588235294) * direction;
    color += image.Sample(sampler, uv) * 0.1964825501511404;
    color += image.Sample(sampler, uv + off1) * 0.2969069646728344;
    color += image.Sample(sampler, uv - off1) * 0.2969069646728344;
    color += image.Sample(sampler, uv + off2) * 0.09447039785044732;
    color += image.Sample(sampler, uv - off2) * 0.09447039785044732;
    color += image.Sample(sampler, uv + off3) * 0.010381362401148057;
    color += image.Sample(sampler, uv - off3) * 0.010381362401148057;
    return color;
}

float4 blur5(Texture2D<float4> image, float2 uv, float2 direction) {
    float4 color = float4(0.0);
    float2 off1 = float2(1.3333333333333333) * direction;
    color += image.Sample(sampler, uv) * 0.29411764705882354;
    color += image.Sample(sampler, uv + off1) * 0.35294117647058826;
    color += image.Sample(sampler, uv - off1) * 0.35294117647058826;
    return color;
}

float4 blur9(Texture2D<float4> image, float2 uv, float2 direction) {
    float4 color = float4(0.0);
    float2 off1 = float2(1.3846153846) * direction;
    float2 off2 = float2(3.2307692308) * direction;
    color += image.Sample(sampler, uv) * 0.2270270270;
    color += image.Sample(sampler, uv + off1) * 0.3162162162;
    color += image.Sample(sampler, uv - off1) * 0.3162162162;
    color += image.Sample(sampler, uv + off2) * 0.0702702703;
    color += image.Sample(sampler, uv - off2) * 0.0702702703;
    return color;
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    if (blur.type == 1) {
        return blur5(source, input.tex_coords, blur.direction);
    }
    if (blur.type == 2) {
        return blur9(source, input.tex_coords, blur.direction);
    }
    if (blur.type == 3) {
        return blur13(source, input.tex_coords, blur.direction);
    }
    return blur5(source, input.tex_coords, blur.direction);
}
