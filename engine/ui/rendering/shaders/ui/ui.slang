struct UiFrame {
    float2 scale;
    float2 translate;
};

struct UiVertexInput {
    [vk::location(0)] float2 position : POSITION;
    [vk::location(1)] float2 uv : TEXCOORD0;
    [vk::location(2)] float4 color : COLOR0;
    [vk::location(3)] float2 local_position : TEXCOORD1;
    [vk::location(4)] float2 size : TEXCOORD2;
    [vk::location(5)] float4 border_radius : TEXCOORD3;
    [vk::location(6)] float4 border : TEXCOORD4;
    [vk::location(7)] float kind : TEXCOORD5;
    [vk::location(8)] float border_side : TEXCOORD6;
};

struct UiFragmentInput {
    float4 position : SV_Position;
    [vk::location(0)] float2 uv : TEXCOORD0;
    [vk::location(1)] float4 color : COLOR0;
    [vk::location(2)] float2 local_position : TEXCOORD1;
    [vk::location(3)] float2 size : TEXCOORD2;
    [vk::location(4)] float4 border_radius : TEXCOORD3;
    [vk::location(5)] float4 border : TEXCOORD4;
    [vk::location(6)] float kind : TEXCOORD5;
    [vk::location(7)] float border_side : TEXCOORD6;
};

layout(set = 0, binding = 0) ConstantBuffer<UiFrame> frame;
layout(set = 1, binding = 0) Texture2D<float4> image;
layout(set = 1, binding = 1) SamplerState image_sampler;

[shader("vertex")]
UiFragmentInput vertex_main(UiVertexInput input) {
    UiFragmentInput output;
    output.position = float4(
        input.position * frame.scale + frame.translate,
        0.0,
        1.0
    );
    output.uv = input.uv;
    output.color = input.color;
    output.local_position = input.local_position;
    output.size = input.size;
    output.border_radius = input.border_radius;
    output.border = input.border;
    output.kind = input.kind;
    output.border_side = input.border_side;
    return output;
}

float corner_radius(float2 point, float4 radius) {
    if (point.y < 0.0) {
        return point.x < 0.0 ? radius.x : radius.y;
    }
    return point.x < 0.0 ? radius.w : radius.z;
}

float rounded_rect_distance(float2 point, float2 half_size, float4 radius) {
    float r = corner_radius(point, radius);
    float2 q = abs(point) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float coverage(float distance) {
    return 1.0 - smoothstep(-0.5, 0.5, distance);
}

[shader("fragment")]
float4 fragment_main(UiFragmentInput input) : SV_Target0 {
    if (input.kind > 1.5) {
        float glyph_coverage = image.Sample(image_sampler, input.uv).r;
        return float4(input.color.rgb, input.color.a * glyph_coverage);
    }
    float2 half_size = input.size * 0.5;
    float outer = coverage(rounded_rect_distance(
        input.local_position,
        half_size,
        input.border_radius
    ));
    float4 color = image.Sample(image_sampler, input.uv) * input.color;
    if (input.kind < 0.5) {
        color.a *= outer;
        return color;
    }

    float2 inner_min = -half_size + input.border.xy;
    float2 inner_max = half_size - input.border.zw;
    float inner = 0.0;
    if (inner_max.x > inner_min.x && inner_max.y > inner_min.y) {
        float2 inner_center = (inner_min + inner_max) * 0.5;
        float2 inner_half = (inner_max - inner_min) * 0.5;
        float4 inner_radius = max(
            input.border_radius -
                float4(
                    max(input.border.x, input.border.y),
                    max(input.border.z, input.border.y),
                    max(input.border.z, input.border.w),
                    max(input.border.x, input.border.w)
                ),
            0.0
        );
        inner = coverage(rounded_rect_distance(
            input.local_position - inner_center,
            inner_half,
            inner_radius
        ));
    }

    const float far_distance = 1.0e20;
    float left_distance = input.border.x > 0.0 ?
        (input.local_position.x + half_size.x) / input.border.x :
        far_distance;
    float top_distance = input.border.y > 0.0 ?
        (input.local_position.y + half_size.y) / input.border.y :
        far_distance;
    float right_distance = input.border.z > 0.0 ?
        (half_size.x - input.local_position.x) / input.border.z :
        far_distance;
    float bottom_distance = input.border.w > 0.0 ?
        (half_size.y - input.local_position.y) / input.border.w :
        far_distance;

    float owner = 1.0;
    float nearest = left_distance;
    if (top_distance < nearest) {
        owner = 2.0;
        nearest = top_distance;
    }
    if (right_distance < nearest) {
        owner = 3.0;
        nearest = right_distance;
    }
    if (bottom_distance < nearest) {
        owner = 4.0;
    }
    float owns_pixel = abs(owner - input.border_side) < 0.5 ? 1.0 : 0.0;
    color.a *= outer * (1.0 - inner) * owns_pixel;
    return color;
}
