namespace rendering.normal {

public float3 encode(float3 normal) {
    return normal * 0.5 + float3(0.5);
}

public float3 decode(float3 normal) {
    return normal * 2.0 - float3(1.0);
}

public float3 from_tangent_frame(
    float3 tangent_normal,
    float3 normal_ws,
    float3 tangent_ws
) {
    float3 normal = normalize(normal_ws);
    float3 tangent = normalize(tangent_ws);
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    float3 bitangent = normalize(cross(normal, tangent));
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    return normalize(mul(tangent_normal, tbn));
}

public float3 from_derivatives(
    float3 tangent_normal,
    float3 position_ws,
    float3 normal_ws,
    float2 tex_coords
) {
    float3 q1 = ddx(position_ws);
    float3 q2 = ddy(position_ws);
    float2 st1 = ddx(tex_coords);
    float2 st2 = ddy(tex_coords);

    float3 normal = normalize(normal_ws);
    float3 tangent = normalize(q1 * st2.y - q2 * st1.y);
    float3 bitangent = -normalize(cross(normal, tangent));
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    return normalize(mul(tangent_normal, tbn));
}

} // namespace rendering.normal
